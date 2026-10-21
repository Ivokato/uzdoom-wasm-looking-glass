/*
** hw_vrmodes.cpp
** Matrix handling for stereo 3D / Looking Glass multi-view rendering
*/

#include "vectors.h"
#include "hw_cvars.h"
#include "hw_vrmodes.h"
#include "v_video.h"
#include "version.h"
#include "i_interface.h"
#include <cmath>
#include <algorithm>

CVAR(Int, vr_mode, 0, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
CVAR(Bool, vr_swap_eyes, false, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_ipd, 0.062f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Float, vr_screendist, 0.80f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, vr_hunits_per_meter, 41.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// Looking Glass quilt controls (used when vr_mode == VR_LOOKINGGLASS)
CVAR(Int, vr_lg_quality, 24, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)   // sparse eye count
CVAR(Int, vr_lg_columns, 8, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, vr_lg_rows, 6, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, vr_lg_viewcone, 40.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // degrees
CVAR(Float, vr_lg_camdist, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)  // meters
CVAR(Float, vr_lg_depthiness, 1.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, vr_lg_tile_w, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // 0 = auto from screen aspect
CVAR(Int, vr_lg_tile_h, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, vr_lg_screen_w, 1440, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, vr_lg_screen_h, 2560, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Int, vr_lg_long_edge, 512, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // auto tile long edge (px)

#define isqrt2 0.7071067812f

static VRMode vrmi_mono = { 1, 1.f, 1.f, 1.f, { { 0.f, 1.f },{ 0.f, 0.f } } };
static VRMode vrmi_stereo = { 2, 1.f, 1.f, 1.f, { { -.5f, 1.f },{ .5f, 1.f } } };
static VRMode vrmi_sbsfull = { 2, .5f, 1.f, 2.f, { { -.5f, .5f },{ .5f, .5f } } };
static VRMode vrmi_sbssquished = { 2, .5f, 1.f, 1.f, { { -.5f, 1.f },{ .5f, 1.f } } };
static VRMode vrmi_lefteye = { 1, 1.f, 1.f, 1.f, { { -.5f, 1.f },{ 0.f, 0.f } } };
static VRMode vrmi_righteye = { 1, 1.f, 1.f, 1.f, { { .5f, 1.f },{ 0.f, 0.f } } };
static VRMode vrmi_topbottom = { 2, 1.f, .5f, 1.f, { { -.5f, 1.f },{ .5f, 1.f } } };
static VRMode vrmi_checker = { 2, isqrt2, isqrt2, 1.f, { { -.5f, 1.f },{ .5f, 1.f } } };
static VRMode vrmi_lookingglass;

static float DEG2RAD(float deg)
{
	return deg * float(M_PI / 180.0);
}

static float RAD2DEG(float rad)
{
	return rad * float(180. / M_PI);
}

/** Match looking-glass-doom/src/quilt.js selectViewIndices. */
static void SelectViewIndices(int numViews, int qualityCount, int *outIndices, int *outCount)
{
	const int total = std::max(1, numViews);
	int want = std::min(total, std::max(1, qualityCount));
	want = std::min(want, (int)VR_MAX_EYES);

	if (want >= total)
	{
		for (int i = 0; i < total; ++i) outIndices[i] = i;
		*outCount = total;
		return;
	}

	const int center = (total - 1) / 2;
	bool used[VR_MAX_EYES * 2];
	// numViews can be up to columns*rows; clamp used[] size via set logic
	int count = 0;
	auto add = [&](int idx) {
		if (idx < 0 || idx >= total) return;
		for (int i = 0; i < count; ++i)
			if (outIndices[i] == idx) return;
		outIndices[count++] = idx;
	};

	add(center);
	if (want == 1)
	{
		*outCount = count;
		return;
	}
	for (int i = 0; i < want - 1 && count < want; ++i)
	{
		float t = (want == 1) ? 0.f : (float)i / (float)(want - 1);
		add((int)std::round(t * (total - 1)));
	}
	int probe = 0;
	while (count < want && probe < total * 2)
	{
		int idx = (center + (int)std::ceil(probe / 2.0) * (probe % 2 == 0 ? 1 : -1) + total) % total;
		add(idx);
		probe++;
	}
	std::sort(outIndices, outIndices + count);
	*outCount = count;
	(void)used;
}

static void UpdateLookingGlassMode()
{
	int cols = std::clamp((int)vr_lg_columns, 1, 16);
	int rows = std::clamp((int)vr_lg_rows, 1, 16);
	int numViews = std::min(cols * rows, (int)VR_MAX_EYES);
	int quality = std::clamp((int)vr_lg_quality, 1, numViews);

	int indices[VR_MAX_EYES];
	int eyeCount = 0;
	SelectViewIndices(numViews, quality, indices, &eyeCount);

	float cone = DEG2RAD(vr_lg_viewcone) * std::max(0.01f, (float)vr_lg_depthiness);
	float camDist = std::max(0.01f, (float)vr_lg_camdist);

	vrmi_lookingglass.mEyeCount = eyeCount;
	vrmi_lookingglass.mHorizontalViewportScale = 1.f;
	vrmi_lookingglass.mVerticalViewportScale = 1.f;
	vrmi_lookingglass.mWeaponProjectionScale = 1.f;
	vrmi_lookingglass.mQuiltColumns = cols;
	vrmi_lookingglass.mQuiltRows = rows;

	for (int e = 0; e < eyeCount; ++e)
	{
		int viewIndex = indices[e];
		float f = (viewIndex + 0.5f) / (float)numViews - 0.5f;
		float F = tanf(cone * f);
		vrmi_lookingglass.mQuiltIndices[e] = viewIndex;
		vrmi_lookingglass.mEyes[e].mShiftFactor = 0.f;
		vrmi_lookingglass.mEyes[e].mScaleFactor = 1.f;
		vrmi_lookingglass.mEyes[e].mLgFrustumF = F;
		vrmi_lookingglass.mEyes[e].mLgLateralM = camDist * F;
	}
}

const VRMode *VRMode::GetVRMode(bool toscreen)
{
	int mode = !toscreen || (sysCallbacks.DisableTextureFilter && sysCallbacks.DisableTextureFilter()) ? 0 : vr_mode;

	switch (mode)
	{
	default:
	case VR_MONO:
		return &vrmi_mono;

	case VR_GREENMAGENTA:
	case VR_REDCYAN:
	case VR_QUADSTEREO:
	case VR_AMBERBLUE:
	case VR_SIDEBYSIDELETTERBOX:
		return &vrmi_stereo;

	case VR_SIDEBYSIDESQUISHED:
	case VR_COLUMNINTERLEAVED:
		return &vrmi_sbssquished;

	case VR_SIDEBYSIDEFULL:
		return &vrmi_sbsfull;

	case VR_TOPBOTTOM:
	case VR_ROWINTERLEAVED:
		return &vrmi_topbottom;

	case VR_LEFTEYEVIEW:
		return &vrmi_lefteye;

	case VR_RIGHTEYEVIEW:
		return &vrmi_righteye;

	case VR_CHECKERINTERLEAVED:
		return &vrmi_checker;

	case VR_LOOKINGGLASS:
		UpdateLookingGlassMode();
		return &vrmi_lookingglass;
	}
}

void VRMode::AdjustViewport(DFrameBuffer *screen) const
{
	screen->mSceneViewport.height = (int)(screen->mSceneViewport.height * mVerticalViewportScale);
	screen->mSceneViewport.top = (int)(screen->mSceneViewport.top * mVerticalViewportScale);
	screen->mSceneViewport.width = (int)(screen->mSceneViewport.width * mHorizontalViewportScale);
	screen->mSceneViewport.left = (int)(screen->mSceneViewport.left * mHorizontalViewportScale);

	screen->mScreenViewport.height = (int)(screen->mScreenViewport.height * mVerticalViewportScale);
	screen->mScreenViewport.top = (int)(screen->mScreenViewport.top * mVerticalViewportScale);
	screen->mScreenViewport.width = (int)(screen->mScreenViewport.width * mHorizontalViewportScale);
	screen->mScreenViewport.left = (int)(screen->mScreenViewport.left * mHorizontalViewportScale);
}

VSMatrix VRMode::GetHUDSpriteProjection() const
{
	VSMatrix mat;
	int w = screen->GetWidth();
	int h = screen->GetHeight();
	float scaled_w = w / mWeaponProjectionScale;
	float left_ofs = (w - scaled_w) / 2.f;
	mat.ortho(left_ofs, left_ofs + scaled_w, (float)h, 0, -1.0f, 1.0f);
	return mat;
}

float VREyeInfo::getShift() const
{
	auto res = mShiftFactor * vr_ipd;
	return vr_swap_eyes ? -res : res;
}

VSMatrix VREyeInfo::GetProjection(float fov, float aspectRatio, float fovRatio, bool iso_ortho) const
{
	VSMatrix result;

	if (iso_ortho)
	{
		double zNear = -3.0/fovRatio;
		double zFar = screen->GetZFar();

		double fH = tan(DEG2RAD(fov) / 2) / fovRatio;
		double fW = fH * aspectRatio * mScaleFactor;
		double left = -fW;
		double right = fW;
		double bottom = -fH;
		double top = fH;

		VSMatrix fmat(1);
		fmat.ortho((float)left, (float)right, (float)bottom, (float)top, (float)zNear, (float)zFar);
		return fmat;
	}
	else if (vr_mode == VR_LOOKINGGLASS || mLgFrustumF != 0.f)
	{
		// Looking Glass WebXR: near_x/near = -F + cam*halfTan (parallel cams + off-axis).
		// Center view (F=0) still uses this path so frustum matches siblings.
		double zNear = screen->GetZNear();
		double zFar = screen->GetZFar();
		double F = mLgFrustumF;
		double frustumShift = zNear * F;
		double fH = zNear * tan(DEG2RAD(fov) / 2) / fovRatio;
		double fW = fH * aspectRatio * mScaleFactor;
		double left = -fW - frustumShift;
		double right = fW - frustumShift;
		double bottom = -fH;
		double top = fH;

		VSMatrix fmat(1);
		fmat.frustum((float)left, (float)right, (float)bottom, (float)top, (float)zNear, (float)zFar);
		return fmat;
	}
	else if (mShiftFactor == 0)
	{
		float fovy = (float)(2 * RAD2DEG(atan(tan(DEG2RAD(fov) / 2) / fovRatio)));
		result.perspective(fovy, aspectRatio, screen->GetZNear(), screen->GetZFar());
		return result;
	}
	else
	{
		double zNear = screen->GetZNear();
		double zFar = screen->GetZFar();

		double frustumShift = zNear * getShift() / vr_screendist;
		double fH = zNear * tan(DEG2RAD(fov) / 2) / fovRatio;
		double fW = fH * aspectRatio * mScaleFactor;
		double left = -fW - frustumShift;
		double right = fW - frustumShift;
		double bottom = -fH;
		double top = fH;

		VSMatrix fmat(1);
		fmat.frustum((float)left, (float)right, (float)bottom, (float)top, (float)zNear, (float)zFar);
		return fmat;
	}
}

DVector3 VREyeInfo::GetViewShift(float yaw) const
{
	if (vr_mode == VR_LOOKINGGLASS || mLgFrustumF != 0.f || mLgLateralM != 0.f)
	{
		double lateral = mLgLateralM;
		if (vr_swap_eyes) lateral = -lateral;
		double dx = -cos(DEG2RAD(yaw)) * vr_hunits_per_meter * lateral;
		double dy = sin(DEG2RAD(yaw)) * vr_hunits_per_meter * lateral;
		return { dx, dy, 0 };
	}
	if (mShiftFactor == 0)
	{
		return { 0,0,0 };
	}
	else
	{
		double dx = -cos(DEG2RAD(yaw)) * vr_hunits_per_meter * getShift();
		double dy = sin(DEG2RAD(yaw)) * vr_hunits_per_meter * getShift();
		return { dx, dy, 0 };
	}
}
