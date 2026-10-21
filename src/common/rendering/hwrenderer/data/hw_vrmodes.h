#pragma once

#include "matrix.h"

class DFrameBuffer;

enum
{
	VR_MONO = 0,
	VR_GREENMAGENTA = 1,
	VR_REDCYAN = 2,
	VR_SIDEBYSIDEFULL = 3,
	VR_SIDEBYSIDESQUISHED = 4,
	VR_LEFTEYEVIEW = 5,
	VR_RIGHTEYEVIEW = 6,
	VR_QUADSTEREO = 7,
	VR_SIDEBYSIDELETTERBOX = 8,
	VR_AMBERBLUE = 9,
	VR_TOPBOTTOM = 11,
	VR_ROWINTERLEAVED = 12,
	VR_COLUMNINTERLEAVED = 13,
	VR_CHECKERINTERLEAVED = 14,
	/** Looking Glass quilt: N parallel cameras + off-axis frusta. */
	VR_LOOKINGGLASS = 15,
};

/** Hard cap on quilt / sparse eyes (device quilts are typically ≤48). */
enum { VR_MAX_EYES = 64 };

struct VREyeInfo
{
	float mShiftFactor;
	float mScaleFactor;
	/** Looking Glass: tan(viewCone * f). Non-zero selects LG projection path. */
	float mLgFrustumF = 0.f;
	/** Looking Glass: camera lateral offset in meters (view-local +X). */
	float mLgLateralM = 0.f;

	VSMatrix GetProjection(float fov, float aspectRatio, float fovRatio, bool iso_ortho) const;
	DVector3 GetViewShift(float yaw) const;
private:
	float getShift() const;

};

struct VRMode
{
	int mEyeCount;
	float mHorizontalViewportScale;
	float mVerticalViewportScale;
	float mWeaponProjectionScale;
	VREyeInfo mEyes[VR_MAX_EYES];
	/** Quilt slot each eye renders into (sparse quality → full columns×rows). */
	int mQuiltIndices[VR_MAX_EYES];
	int mQuiltColumns = 1;
	int mQuiltRows = 1;

	static const VRMode *GetVRMode(bool toscreen = true);
	void AdjustViewport(DFrameBuffer *fb) const;
	VSMatrix GetHUDSpriteProjection() const;

	bool IsLookingGlass() const { return mQuiltColumns * mQuiltRows > 1 && mEyeCount > 1; }
};
