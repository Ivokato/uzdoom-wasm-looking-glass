/*
**  Postprocessing framework
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
*/

#include "gles_system.h"
#include "m_png.h"
#include "gles_buffers.h"
#include "gles_framebuffer.h"
#include "gles_renderbuffers.h"
#include "gles_renderer.h"
#include "gles_postprocessstate.h"
#include "gles_shaderprogram.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "hwrenderer/postprocessing/hw_postprocess_cvars.h"
#include "flatvertices.h"
#include "r_videoscale.h"
#include "v_video.h"

#include "hw_vrmodes.h"
#include "v_draw.h"
#include "menustate.h"
#include "c_console.h"
#include "gamestate.h"
#include "sbar.h"
#include "doomstat.h"
#include "d_player.h"
#include "d_main.h"
#include "r_utility.h"
#include "v_palette.h"
#include "gles_renderstate.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern bool vid_hdr_active;
extern bool DrawFSHUD;

EXTERN_CVAR(Int, vr_mode);
EXTERN_CVAR(Int, vr_lg_screen_w);
EXTERN_CVAR(Int, vr_lg_screen_h);
EXTERN_CVAR(Bool, hud_althud);
EXTERN_CVAR(Int, screenblocks);

namespace OpenGLESRenderer
{

namespace
{
	void LookingGlassHangBreadcrumb(const char *where, int flat)
	{
#ifdef __EMSCRIPTEN__
		EM_ASM({
			var h = Module.__lgHang || (Module.__lgHang = {n:0, last:0, where:'', gs:-1, flat:0, path:''});
			h.n++;
			h.last = (typeof performance !== 'undefined') ? performance.now() : Date.now();
			h.where = UTF8ToString($0);
			h.gs = $1;
			h.flat = $2;
			h.path = UTF8ToString($0);
		}, where, (int)gamestate, flat);
#endif
	}

	/** Full-screen 2D modes where multi-view 3D is not useful. */
	bool LookingGlassNeedsFlatPanel()
	{
		if (menuactive != MENU_Off)
			return true;
		if (ConsoleState == c_down || ConsoleState == c_falling)
			return true;
		if (gamestate == GS_INTERMISSION
			|| gamestate == GS_FINALE
			|| gamestate == GS_CUTSCENE
			|| gamestate == GS_INTRO
			|| gamestate == GS_DEMOSCREEN
			|| gamestate == GS_FULLCONSOLE
			|| gamestate == GS_STARTUP
			|| gamestate == GS_TITLELEVEL)
			return true;
		if (automapactive)
			return true;
		return false;
	}

	/** Fallback quilt stamp: also cover pickup text / screen blends. */
	bool LookingGlassNeedsQuiltUiOverlay()
	{
		if (LookingGlassNeedsFlatPanel())
			return true;
		if (StatusBar && StatusBar->HasHUDMessages())
			return true;
		const player_t *p = &players[consoleplayer];
		if (p->bonuscount || p->damagecount || p->BlendA > 0.01f)
			return true;
		return false;
	}

	void LookingGlassBlitSceneToPanel()
	{
		const int pw = std::max(64, (int)vr_lg_screen_w);
		const int ph = std::max(64, (int)vr_lg_screen_h);
		GLRenderer->mBuffers->BlitLookingGlassSceneToPanel(pw, ph);
	}

	/**
	 * Draw HUD / pickup text / blends at native panel resolution onto the
	 * default framebuffer (already holding the lenticular image).
	 * Must NOT call screen->Draw2D() — that rebinds the scene FBO.
	 */
	void LookingGlassDrawPanelHud()
	{
		const int pw = std::max(64, (int)vr_lg_screen_w);
		const int ph = std::max(64, (int)vr_lg_screen_h);

		twod->Clear();
		twod->Begin(pw, ph);

		if ((gamestate == GS_LEVEL || gamestate == GS_TITLELEVEL) && !hud_toggled && StatusBar)
		{
			sector_t *viewsec = nullptr;
			if (players[consoleplayer].camera)
				viewsec = players[consoleplayer].camera->Sector;
			V_DrawBlend(viewsec);

			auto &vp = r_viewpoint;
			if (hud_althud && viewheight == SCREENHEIGHT && screenblocks > 10)
			{
				StatusBar->DrawBottomStuff(HUD_AltHud);
				if (DrawFSHUD || automapactive) StatusBar->DrawAltHUD();
				if (players[consoleplayer].camera && players[consoleplayer].camera->player && !automapactive)
					StatusBar->DrawCrosshair(vp.TicFrac);
				StatusBar->CallDraw(HUD_AltHud, vp.TicFrac);
				StatusBar->DrawTopStuff(HUD_AltHud);
			}
			else if (viewheight == SCREENHEIGHT && viewactive && screenblocks > 10)
			{
				EHudState state = DrawFSHUD ? HUD_Fullscreen : HUD_None;
				StatusBar->DrawBottomStuff(state);
				StatusBar->CallDraw(state, vp.TicFrac);
				StatusBar->DrawTopStuff(state);
			}
			else
			{
				StatusBar->DrawBottomStuff(HUD_StatusBar);
				StatusBar->CallDraw(HUD_StatusBar, vp.TicFrac);
				StatusBar->DrawTopStuff(HUD_StatusBar);
			}
		}

		// Pickup / Printf notify text lives in the console notify buffer
		// (con_notifytime ≈ 3s). d_main already queued it into twod, but we
		// cleared that queue above — redraw here so it lasts the full duration
		// instead of vanishing with the brief bonuscount flash.
		C_DrawConsole();

		// Draw into the swizzled panel (FB 0), not the scene color buffer.
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, pw, ph);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		::Draw2D(twod, gl_RenderState);
		twod->Clear();
	}
}

int gl_dither_bpc = -1;

void FGLRenderer::RenderScreenQuad()
{
	auto buffer = static_cast<GLVertexBuffer *>(screen->mVertexData->GetBufferObjects().first);
	buffer->Bind(nullptr);
	glDrawArrays(GL_TRIANGLE_STRIP, FFlatVertexBuffer::PRESENT_INDEX, 3);
}

void FGLRenderer::PostProcessScene(int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D)
{
#ifndef NO_RENDER_BUFFER
	mBuffers->BindCurrentFB();
#endif
	if (afterBloomDrawEndScene2D) afterBloomDrawEndScene2D();
}



//-----------------------------------------------------------------------------
//
// Copies the rendered screen to its final destination
//
//-----------------------------------------------------------------------------

void FGLRenderer::Flush()
{
	CopyToBackbuffer(nullptr, true);
}

//-----------------------------------------------------------------------------
//
// Gamma correct while copying to frame buffer
//
//-----------------------------------------------------------------------------

void FGLRenderer::CopyToBackbuffer(const IntRect *bounds, bool applyGamma)
{
#ifdef NO_RENDER_BUFFER
	mBuffers->BindOutputFB();
#endif
	const bool lg = vr_mode == VR_LOOKINGGLASS && mBuffers && mBuffers->HasLookingGlassQuilt();
	const bool lgFlat = lg && LookingGlassNeedsFlatPanel();

	/*
	 * Looking Glass UI policy:
	 *   Stamping HUD into quilt tiles makes text tiny (tile resolution) and any
	 *   misalignment shows as vantage-dependent wobble after swizzle.
	 *   Prefer: swizzle 3D quilt → panel, then draw HUD at native panel size
	 *   (zero parallax). Flat screens (menus/intermission) skip swizzle and
	 *   upscale the scene 2D to the panel.
	 *
	 *   Never nearest-fill on flat frames — that path uses the scene FB as
	 *   scratch (wiping the UI we are about to blit) and is dozens of WebGL
	 *   blits per frame. Under PROXY_TO_PTHREAD that can stall the game when
	 *   entering intermission / end-level.
	 */
	if (lgFlat)
	{
		LookingGlassHangBreadcrumb("present-flat", 1);
		mBuffers->BindCurrentFB();
		glViewport(0, 0, mBuffers->GetSceneWidth(), mBuffers->GetSceneHeight());
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		screen->Draw2D();
		twod->Clear();
		mBuffers->BindOutputFB();
		LookingGlassBlitSceneToPanel();
		LookingGlassHangBreadcrumb("present-flat-done", 1);
		// Canvas is a normal 2D frame — parent must NOT presentNativeFrame
		// (that path assumes lenticular RGB and turns menus into rainbow noise).
		EM_ASM({
			if (Module.lgSwizzle && typeof Module.lgSwizzle.setPresentMode === 'function') {
				Module.lgSwizzle.setPresentMode('flat');
			} else {
				Module.__lgPresentMode = 'flat';
				try {
					parent.postMessage({ type: 'lg-present-mode', mode: 'flat' }, '*');
				} catch (e) {}
			}
		});
		return;
	}

	// Looking Glass 3D: blit the last eye (NextEye skips it), nearest-fill sparse slots.
	if (lg)
	{
		LookingGlassHangBreadcrumb("present-3d", 0);
		auto *fb = static_cast<OpenGLFrameBuffer *>(framebuffer);
		fb->LookingGlassBlitCurrentEye();
		auto vrmode = VRMode::GetVRMode(true);
		mBuffers->NearestFillLookingGlassQuilt(
			vrmode->mQuiltIndices, vrmode->mEyeCount,
			vrmode->mQuiltColumns, vrmode->mQuiltRows);
	}

#ifdef __EMSCRIPTEN__
	if (lg)
	{
		// 3D quilt only (no UI stamp). Drop the tile-sized 2D queue from d_main
		// and redraw HUD at panel resolution after swizzle.
		mBuffers->BindOutputFB();
		const int texId = (int)mBuffers->GetLookingGlassQuiltTextureName();
		const int qw = mBuffers->GetQuiltWidth();
		const int qh = mBuffers->GetQuiltHeight();
		const int cols = mBuffers->GetQuiltColumns();
		const int rows = mBuffers->GetQuiltRows();
		mBuffers->BindLookingGlassQuiltTexture(0, GL_LINEAR);
		const int ok = EM_ASM_INT({
			if (!Module.lgSwizzle) return 0;
			try {
				var ok = Module.lgSwizzle.present($0, $1, $2, $3, $4) ? 1 : 0;
				// present() itself calls setPresentMode('swizzled') on success.
				return ok;
			} catch (e) {
				console.warn('[lg-swizzle] present failed', e);
				return 0;
			}
		}, texId, qw, qh, cols, rows);
		if (ok)
		{
			twod->Clear();
			LookingGlassDrawPanelHud();
			return;
		}
	}
#endif

	// Fallback: draw 2D into scene and stamp onto quilt tiles (old path).
	if (lg)
		mBuffers->BindCurrentFB();

	screen->Draw2D();
	twod->Clear();

	if (lg)
	{
		if (LookingGlassNeedsQuiltUiOverlay())
			mBuffers->OverlaySceneOnAllLookingGlassTiles();
		else
			mBuffers->OverlayHudStripOnLookingGlassTiles();
	}

	FGLPostProcessState savedState;
	savedState.SaveTextureBindings(2);

	mBuffers->BindOutputFB();

	IntRect box;
	if (bounds)
	{
		box = *bounds;
	}
	else if (lg)
	{
		// Fallback: present the FULL quilt into the client area.
		const int cw = std::max(1, framebuffer->GetClientWidth());
		const int ch = std::max(1, framebuffer->GetClientHeight());
		box.left = 0;
		box.top = 0;
		box.width = cw;
		box.height = ch;
		glViewport(0, 0, cw, ch);
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	else
	{
		ClearBorders();
		box = screen->mOutputLetterbox;
	}

	if (lg)
		mBuffers->BindLookingGlassQuiltTexture(0);
	else
		mBuffers->BindCurrentTexture(0);
#ifndef NO_RENDER_BUFFER
	DrawPresentTexture(box, applyGamma);
#endif
}

void FGLRenderer::DrawPresentTexture(const IntRect &box, bool applyGamma)
{
	glViewport(box.left, box.top, box.width, box.height);

	mBuffers->BindDitherTexture(1);

	glActiveTexture(GL_TEXTURE0);
	if (ViewportLinearScale())
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	mPresentShader->Bind();
	if (!applyGamma || framebuffer->IsHWGammaActive())
	{
		mPresentShader->Uniforms->InvGamma = 1.0f;
		mPresentShader->Uniforms->Contrast = 1.0f;
		mPresentShader->Uniforms->Brightness = 0.0f;
		mPresentShader->Uniforms->Saturation = 1.0f;
	}
	else
	{
		mPresentShader->Uniforms->InvGamma = 1.0f / clamp<float>(vid_gamma, 0.1f, 4.f);
		mPresentShader->Uniforms->Contrast = clamp<float>(vid_contrast, 0.1f, 3.f);
		mPresentShader->Uniforms->Brightness = clamp<float>(vid_brightness, -0.8f, 0.8f);
		mPresentShader->Uniforms->Saturation = clamp<float>(vid_saturation, -15.0f, 15.f);
		mPresentShader->Uniforms->GrayFormula = static_cast<int>(gl_satformula);
	}
	if (vid_hdr_active && framebuffer->IsFullscreen())
	{
		// Full screen exclusive mode treats a rgba16f frame buffer as linear.
		// It probably will eventually in desktop mode too, but the DWM doesn't seem to support that.
		mPresentShader->Uniforms->HdrMode = 1;
		mPresentShader->Uniforms->ColorScale = (gl_dither_bpc == -1) ? 1023.0f : (float)((1 << gl_dither_bpc) - 1);
	}
	else
	{
		mPresentShader->Uniforms->HdrMode = 0;
		mPresentShader->Uniforms->ColorScale = (gl_dither_bpc == -1) ? 255.0f : (float)((1 << gl_dither_bpc) - 1);
	}
	if (vr_mode == VR_LOOKINGGLASS && mBuffers->HasLookingGlassQuilt())
	{
		// Present the full quilt texture (not a cropped scene viewport).
		mPresentShader->Uniforms->Scale = { 1.0f, 1.0f };
	}
	else
	{
		mPresentShader->Uniforms->Scale = { screen->mScreenViewport.width / (float)mBuffers->GetWidth(), screen->mScreenViewport.height / (float)mBuffers->GetHeight() };
	}
	mPresentShader->Uniforms->Offset = { 0.0f, 0.0f };
	mPresentShader->Uniforms.SetData();


	for (size_t n = 0; n < mPresentShader->Uniforms.mFields.size(); n++)
	{
		UniformFieldDesc desc = mPresentShader->Uniforms.mFields[n];
		int loc = mPresentShader->Uniforms.UniformLocation[n];
		switch (desc.Type)
		{
		case UniformType::Int:
			glUniform1i(loc, *((GLint*)(((char*)(&mPresentShader->Uniforms)) + desc.Offset)));
			break;
		case UniformType::Float:
			glUniform1f(loc, *((GLfloat*)(((char*)(&mPresentShader->Uniforms)) + desc.Offset)));
			break;
		case UniformType::Vec2:
			glUniform2fv(loc,1 , ((GLfloat*)(((char*)(&mPresentShader->Uniforms)) + desc.Offset)));
			break;
		default:
			break;
		}
	}

	RenderScreenQuad();
}

//-----------------------------------------------------------------------------
//
// Fills the black bars around the screen letterbox
//
//-----------------------------------------------------------------------------

void FGLRenderer::ClearBorders()
{
	const auto &box = screen->mOutputLetterbox;

	int clientWidth = framebuffer->GetClientWidth();
	int clientHeight = framebuffer->GetClientHeight();
	if (clientWidth == 0 || clientHeight == 0)
		return;

	glViewport(0, 0, clientWidth, clientHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glEnable(GL_SCISSOR_TEST);
	if (box.top > 0)
	{
		glScissor(0, 0, clientWidth, box.top);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	if (clientHeight - box.top - box.height > 0)
	{
		glScissor(0, box.top + box.height, clientWidth, clientHeight - box.top - box.height);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	if (box.left > 0)
	{
		glScissor(0, box.top, box.left, box.height);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	if (clientWidth - box.left - box.width > 0)
	{
		glScissor(box.left + box.width, box.top, clientWidth - box.left - box.width, box.height);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glDisable(GL_SCISSOR_TEST);
}

}
