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
#include "v_video.h"
#include "printf.h"
#include "hw_cvars.h"
#include "gles_renderer.h"
#include "gles_renderbuffers.h"
#include "gles_postprocessstate.h"
#include "gles_shaderprogram.h"
#include "gles_buffers.h"
#include "sbar.h"
#include "v_draw.h"

#include <random>
#include <algorithm>
#include <cstdlib>
#include <cmath>

EXTERN_CVAR(Int, gl_debug_level)

namespace OpenGLESRenderer
{

	//==========================================================================
	//
	// Initialize render buffers and textures used in rendering passes
	//
	//==========================================================================

	FGLRenderBuffers::FGLRenderBuffers()
	{

	}

	//==========================================================================
	//
	// Free render buffer resources
	//
	//==========================================================================

	FGLRenderBuffers::~FGLRenderBuffers()
	{
		ClearScene();
		ClearQuilt();

		DeleteTexture(mDitherTexture);
	}

	void FGLRenderBuffers::ClearQuilt()
	{
		DeleteFrameBuffer(mQuiltFB);
		DeleteTexture(mQuiltTex);
		mQuiltWidth = mQuiltHeight = 0;
		mQuiltColumns = mQuiltRows = 0;
		mQuiltTileW = mQuiltTileH = 0;
	}

	void FGLRenderBuffers::ClearScene()
	{
		DeleteFrameBuffer(mSceneFB);
		DeleteRenderBuffer(mSceneDepthStencilBuf);
		DeleteRenderBuffer(mSceneStencilBuf);
	}

	void FGLRenderBuffers::DeleteTexture(PPGLTexture& tex)
	{
		if (tex.handle != 0)
			glDeleteTextures(1, &tex.handle);
		tex.handle = 0;
	}

	void FGLRenderBuffers::DeleteRenderBuffer(PPGLRenderBuffer& buf)
	{
		if (buf.handle != 0)
			glDeleteRenderbuffers(1, &buf.handle);
		buf.handle = 0;
	}

	void FGLRenderBuffers::DeleteFrameBuffer(PPGLFrameBuffer& fb)
	{
		if (fb.handle != 0)
			glDeleteFramebuffers(1, &fb.handle);
		fb.handle = 0;
	}

	//==========================================================================
	//
	// Makes sure all render buffers have sizes suitable for rending at the
	// specified resolution
	//
	//==========================================================================

	void FGLRenderBuffers::Setup(int width, int height, int sceneWidth, int sceneHeight)
	{
		if (width <= 0 || height <= 0)
			I_FatalError("Requested invalid render buffer sizes: screen = %dx%d", width, height);

		GLint activeTex;
		GLint textureBinding;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding);

		if (width != mWidth || height != mHeight)
			CreatePipeline(width, height);

		if (width != mWidth || height != mHeight )
			CreateScene(width, height);

		mWidth = width;
		mHeight = height;
		mSceneWidth = sceneWidth;
		mSceneHeight = sceneHeight;

		glBindTexture(GL_TEXTURE_2D, textureBinding);
		glActiveTexture(activeTex);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		if (FailedCreate)
		{
			ClearScene();
			mWidth = 0;
			mHeight = 0;
			mSceneWidth = 0;
			mSceneHeight = 0;
			I_FatalError("Unable to create render buffers.");
		}
	}

	//==========================================================================
	//
	// Creates the scene buffers
	//
	//==========================================================================

	void FGLRenderBuffers::CreateScene(int width, int height)
	{
		ClearScene();
		if(gles.depthStencilAvailable)
			mSceneDepthStencilBuf = CreateRenderBuffer("SceneDepthStencil", GL_DEPTH24_STENCIL8, width, height);
		else
		{
			mSceneDepthStencilBuf = CreateRenderBuffer("SceneDepthStencil", GL_DEPTH_COMPONENT16, width, height);
			mSceneStencilBuf = CreateRenderBuffer("SceneStencil", GL_STENCIL_INDEX8, width, height);
		}
		mSceneFB= CreateFrameBuffer("SceneFB", mSceneTex, mSceneDepthStencilBuf, mSceneStencilBuf);
	}

	//==========================================================================
	//
	// Creates the buffers needed for post processing steps
	//
	//==========================================================================

	void FGLRenderBuffers::CreatePipeline(int width, int height)
	{
		mSceneTex = Create2DTexture("PipelineTexture", GL_RGBA, width, height);
	}



	//==========================================================================
	//
	// Creates a 2D texture defaulting to linear filtering and clamp to edge
	//
	//==========================================================================

	PPGLTexture FGLRenderBuffers::Create2DTexture(const char* name, GLuint format, int width, int height, const void* data)
	{
		PPGLTexture tex;
		tex.Width = width;
		tex.Height = height;
		glGenTextures(1, &tex.handle);
		glBindTexture(GL_TEXTURE_2D, tex.handle);

		GLenum dataformat = 0, datatype = 0;
		/*
		switch (format)
		{
		case GL_RGBA:				dataformat = GL_RGBA; datatype = GL_UNSIGNED_BYTE; break;

		case GL_RGBA16:				dataformat = GL_RGBA; datatype = GL_UNSIGNED_SHORT; break;
		case GL_RGBA16F:			dataformat = GL_RGBA; datatype = GL_FLOAT; break;
		case GL_RGBA32F:			dataformat = GL_RGBA; datatype = GL_FLOAT; break;
		case GL_RGBA16_SNORM:		dataformat = GL_RGBA; datatype = GL_SHORT; break;
		case GL_R32F:				dataformat = GL_RED; datatype = GL_FLOAT; break;
		case GL_R16F:				dataformat = GL_RED; datatype = GL_FLOAT; break;
		case GL_RG32F:				dataformat = GL_RG; datatype = GL_FLOAT; break;
		case GL_RG16F:				dataformat = GL_RG; datatype = GL_FLOAT; break;
		case GL_RGB10_A2:			dataformat = GL_RGBA; datatype = GL_UNSIGNED_INT_10_10_10_2; break;
		case GL_DEPTH_COMPONENT24:	dataformat = GL_DEPTH_COMPONENT; datatype = GL_FLOAT; break;

		case GL_STENCIL_INDEX8:		dataformat = GL_STENCIL_INDEX8_OES; datatype = GL_INT; break;
		case GL_DEPTH24_STENCIL8_OES:	dataformat = GL_DEPTH_STENCIL_OES; datatype = GL_UNSIGNED_INT_24_8; break;
		default: I_FatalError("Unknown format passed to FGLRenderBuffers.Create2DTexture");
		}
	*/
		format = GL_RGBA;
		dataformat = GL_RGBA;
		datatype = GL_UNSIGNED_BYTE;

		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, dataformat, datatype, data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return tex;
	}

	//==========================================================================
	//
	// Creates a render buffer
	//
	//==========================================================================

	PPGLRenderBuffer FGLRenderBuffers::CreateRenderBuffer(const char* name, GLuint format, int width, int height)
	{
		PPGLRenderBuffer buf;
		glGenRenderbuffers(1, &buf.handle);
		glBindRenderbuffer(GL_RENDERBUFFER, buf.handle);

		glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
		return buf;
	}

	//==========================================================================
	//
	// Creates a frame buffer
	//
	//==========================================================================

	PPGLFrameBuffer FGLRenderBuffers::CreateFrameBuffer(const char* name, PPGLTexture colorbuffer)
	{
		PPGLFrameBuffer fb;
		glGenFramebuffers(1, &fb.handle);
		glBindFramebuffer(GL_FRAMEBUFFER, fb.handle);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorbuffer.handle, 0);
		if (CheckFrameBufferCompleteness())
			ClearFrameBuffer(false, false);
		return fb;
	}

	PPGLFrameBuffer FGLRenderBuffers::CreateFrameBuffer(const char* name, PPGLTexture colorbuffer, PPGLRenderBuffer depthstencil, PPGLRenderBuffer stencil)
	{
		PPGLFrameBuffer fb;
		glGenFramebuffers(1, &fb.handle);
		glBindFramebuffer(GL_FRAMEBUFFER, fb.handle);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorbuffer.handle, 0);
		if (gles.depthStencilAvailable)
		{
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthstencil.handle);
		}
		else
		{
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthstencil.handle);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil.handle);
		}
		if (CheckFrameBufferCompleteness())
			ClearFrameBuffer(true, true);
		return fb;
	}

	PPGLFrameBuffer FGLRenderBuffers::CreateFrameBuffer(const char* name, PPGLRenderBuffer colorbuffer, PPGLRenderBuffer depthstencil, PPGLRenderBuffer stencil)
	{
		PPGLFrameBuffer fb;
		glGenFramebuffers(1, &fb.handle);
		glBindFramebuffer(GL_FRAMEBUFFER, fb.handle);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorbuffer.handle);
		if (gles.depthStencilAvailable)
		{
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthstencil.handle);
		}
		else
		{
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthstencil.handle);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil.handle);
		}
		if (CheckFrameBufferCompleteness())
			ClearFrameBuffer(true, true);
		return fb;
	}

	PPGLFrameBuffer FGLRenderBuffers::CreateFrameBuffer(const char* name, PPGLTexture colorbuffer0, PPGLTexture colorbuffer1, PPGLTexture colorbuffer2, PPGLTexture depthstencil, PPGLRenderBuffer stencil)
	{
		PPGLFrameBuffer fb;
		glGenFramebuffers(1, &fb.handle);
		glBindFramebuffer(GL_FRAMEBUFFER, fb.handle);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorbuffer0.handle, 0);
		if (gles.depthStencilAvailable)
		{
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthstencil.handle);
		}
		else
		{
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthstencil.handle);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil.handle);
		}

		if (CheckFrameBufferCompleteness())
			ClearFrameBuffer(true, true);
		return fb;
	}

	//==========================================================================
	//
	// Verifies that the frame buffer setup is valid
	//
	//==========================================================================

	bool FGLRenderBuffers::CheckFrameBufferCompleteness()
	{
		GLenum result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (result == GL_FRAMEBUFFER_COMPLETE)
			return true;

		FString error = "glCheckFramebufferStatus failed: ";
		switch (result)
		{
		default: error.AppendFormat("error code %d", (int)result); break;
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: error << "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT"; break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: error << "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT"; break;
		case GL_FRAMEBUFFER_UNSUPPORTED: error << "GL_FRAMEBUFFER_UNSUPPORTED"; break;
		}
		Printf("%s\n", error.GetChars());


		return false;
	}

	//==========================================================================
	//
	// Clear frame buffer to make sure it never contains uninitialized data
	//
	//==========================================================================

	void FGLRenderBuffers::ClearFrameBuffer(bool stencil, bool depth)
	{
		GLboolean scissorEnabled;
		GLint stencilValue;
		GLfloat depthValue;
		glGetBooleanv(GL_SCISSOR_TEST, &scissorEnabled);
		glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &stencilValue);
		glGetFloatv(GL_DEPTH_CLEAR_VALUE, &depthValue);
		glDisable(GL_SCISSOR_TEST);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClearDepthf(0.0f);
		glClearStencil(0);
		GLenum flags = GL_COLOR_BUFFER_BIT;
		if (stencil)
			flags |= GL_STENCIL_BUFFER_BIT;
		if (depth)
			flags |= GL_DEPTH_BUFFER_BIT;
		glClear(flags);
		glClearStencil(stencilValue);
		glClearDepthf(depthValue);
		if (scissorEnabled)
			glEnable(GL_SCISSOR_TEST);
	}


	void FGLRenderBuffers::BindDitherTexture(int texunit)
	{
		if (!mDitherTexture)
		{
			static const float data[64] =
			{
				 .0078125, .2578125, .1328125, .3828125, .0234375, .2734375, .1484375, .3984375,
				 .7578125, .5078125, .8828125, .6328125, .7734375, .5234375, .8984375, .6484375,
				 .0703125, .3203125, .1953125, .4453125, .0859375, .3359375, .2109375, .4609375,
				 .8203125, .5703125, .9453125, .6953125, .8359375, .5859375, .9609375, .7109375,
				 .0390625, .2890625, .1640625, .4140625, .0546875, .3046875, .1796875, .4296875,
				 .7890625, .5390625, .9140625, .6640625, .8046875, .5546875, .9296875, .6796875,
				 .1015625, .3515625, .2265625, .4765625, .1171875, .3671875, .2421875, .4921875,
				 .8515625, .6015625, .9765625, .7265625, .8671875, .6171875, .9921875, .7421875,
			};

			glActiveTexture(GL_TEXTURE0 + texunit);
			mDitherTexture = Create2DTexture("DitherTexture", GL_RGBA, 8, 8, data);
		}
		mDitherTexture.Bind(1, GL_NEAREST, GL_REPEAT);
	}


	//==========================================================================
	//
	// Makes the scene frame buffer active (multisample, depth, stecil, etc.)
	//
	//==========================================================================

	void FGLRenderBuffers::BindSceneFB(bool sceneData)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mSceneFB.handle);
	}

	//==========================================================================
	//
	// Binds the current scene/effect/hud texture to the specified texture unit
	//
	//==========================================================================

	void FGLRenderBuffers::BindCurrentTexture(int index, int filter, int wrap)
	{
		mSceneTex.Bind(index, filter, wrap);
	}

	//==========================================================================
	//
	// Makes the frame buffer for the current texture active 
	//
	//==========================================================================

	void FGLRenderBuffers::BindCurrentFB()
	{
#ifndef NO_RENDER_BUFFER
		mSceneFB.Bind();
#endif
	}

	//==========================================================================
	//
	// Makes the screen frame buffer active
	//
	//==========================================================================

	void FGLRenderBuffers::BindOutputFB()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	//==========================================================================
	//
	// Returns true if render buffers are supported and should be used
	//
	//==========================================================================

	bool FGLRenderBuffers::FailedCreate = false;

	/** Real status-bar band in GL coords (y=0 at bottom), scaled to `height`.
	 *  Portrait tiles make classic 32/200 far too tall — use StatusBar layout. */
	static int LookingGlassHudBarHeight(int height)
	{
		if (height <= 1) return 1;
		int barH = 0;
		if (StatusBar)
		{
			const int top = StatusBar->GetTopOfStatusbar();
			int refH = twod ? twod->GetHeight() : 0;
			if (refH <= 0 && screen) refH = screen->GetHeight();
			if (refH <= 0) refH = height;
			if (top >= 0 && top < refH)
			{
				const int nativeBar = refH - top;
				barH = std::max(1, (nativeBar * height + refH / 2) / refH);
			}
		}
		if (barH <= 0)
			barH = std::max(1, height * 32 / 200);
		return std::min(barH, height - 1);
	}

	void FGLRenderBuffers::EnsureLookingGlassQuilt(int columns, int rows, int tileW, int tileH)
	{
		columns = std::max(1, columns);
		rows = std::max(1, rows);
		tileW = std::max(1, tileW);
		tileH = std::max(1, tileH);
		const int qw = columns * tileW;
		const int qh = rows * tileH;
		if (mQuiltFB && mQuiltWidth == qw && mQuiltHeight == qh
			&& mQuiltColumns == columns && mQuiltRows == rows)
			return;

		ClearQuilt();
		GLint activeTex;
		GLint textureBinding;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
		glActiveTexture(GL_TEXTURE0);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding);

		mQuiltTex = Create2DTexture("LookingGlassQuilt", GL_RGBA, qw, qh);
		mQuiltFB = CreateFrameBuffer("LookingGlassQuiltFB", mQuiltTex);
		mQuiltWidth = qw;
		mQuiltHeight = qh;
		mQuiltColumns = columns;
		mQuiltRows = rows;
		mQuiltTileW = tileW;
		mQuiltTileH = tileH;

		glBindTexture(GL_TEXTURE_2D, textureBinding);
		glActiveTexture(activeTex);
	}

	void FGLRenderBuffers::ClearLookingGlassQuilt()
	{
		if (!mQuiltFB) return;
		mQuiltFB.Bind();
		glViewport(0, 0, mQuiltWidth, mQuiltHeight);
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		BindSceneFB(false);
	}

	void FGLRenderBuffers::BlitSceneToQuiltView(int viewIndex, int columns, int rows)
	{
		if (!mQuiltFB || !mSceneFB || columns < 1 || rows < 1) return;
		const int col = viewIndex % columns;
		const int rowFromBottom = viewIndex / columns;
		if (rowFromBottom >= rows) return;
		const int dx0 = col * mQuiltTileW;
		const int dy0 = rowFromBottom * mQuiltTileH;

		// Keep the classic status-bar band out of multi-view blits. Stretching the
		// full scene (including the bar) into the tile puts weapon/floor pixels in
		// the HUD zone; OverlayHudStrip then replaces that zone with flat 2D from
		// the last eye → horizontal tear through the hand.
		const int srcBarH = LookingGlassHudBarHeight(mSceneHeight);
		const int dstBarH = LookingGlassHudBarHeight(mQuiltTileH);
		if (srcBarH >= mSceneHeight || dstBarH >= mQuiltTileH) return;

		glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFB.handle);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mQuiltFB.handle);
		glBlitFramebuffer(
			0, srcBarH, mSceneWidth, mSceneHeight,
			dx0, dy0 + dstBarH, dx0 + mQuiltTileW, dy0 + mQuiltTileH,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		BindSceneFB(false);
	}

	void FGLRenderBuffers::OverlaySceneOnAllLookingGlassTiles()
	{
		if (!mQuiltFB || !mSceneFB || mQuiltColumns < 1 || mQuiltRows < 1) return;
		// Menus/console: flat full-frame onto every tile (include HUD band).
		const int total = mQuiltColumns * mQuiltRows;
		for (int view = 0; view < total; ++view)
		{
			const int col = view % mQuiltColumns;
			const int rowFromBottom = view / mQuiltColumns;
			const int dx0 = col * mQuiltTileW;
			const int dy0 = rowFromBottom * mQuiltTileH;
			glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFB.handle);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mQuiltFB.handle);
			glBlitFramebuffer(
				0, 0, mSceneWidth, mSceneHeight,
				dx0, dy0, dx0 + mQuiltTileW, dy0 + mQuiltTileH,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		BindSceneFB(false);
	}

	void FGLRenderBuffers::OverlayHudStripOnLookingGlassTiles()
	{
		if (!mQuiltFB || !mSceneFB || mQuiltColumns < 1 || mQuiltRows < 1) return;
		if (mSceneWidth <= 0 || mSceneHeight <= 0) return;

		const int barH = LookingGlassHudBarHeight(mSceneHeight);
		const int destBarH = LookingGlassHudBarHeight(mQuiltTileH);
		const int total = mQuiltColumns * mQuiltRows;
		for (int view = 0; view < total; ++view)
		{
			const int col = view % mQuiltColumns;
			const int rowFromBottom = view / mQuiltColumns;
			const int dx0 = col * mQuiltTileW;
			const int dy0 = rowFromBottom * mQuiltTileH;

			glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFB.handle);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mQuiltFB.handle);
			glBlitFramebuffer(
				0, 0, mSceneWidth, barH,
				dx0, dy0, dx0 + mQuiltTileW, dy0 + destBarH,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		BindSceneFB(false);
	}

	void FGLRenderBuffers::BlitLookingGlassSceneToPanel(int panelW, int panelH)
	{
		if (!mSceneFB || mSceneWidth < 1 || mSceneHeight < 1) return;
		panelW = std::max(1, panelW);
		panelH = std::max(1, panelH);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFB.handle);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glViewport(0, 0, panelW, panelH);
		glBlitFramebuffer(
			0, 0, mSceneWidth, mSceneHeight,
			0, 0, panelW, panelH,
			GL_COLOR_BUFFER_BIT, GL_LINEAR);
		BindSceneFB(false);
	}

	void FGLRenderBuffers::NearestFillLookingGlassQuilt(const int *renderedViews, int renderedCount, int columns, int rows)
	{
		// WebGL2 forbids (or no-ops) blitting within the same FBO. Route each
		// copy through the scene color buffer as a scratch target — safe here
		// because nearest-fill runs after the last eye has already been
		// captured into the quilt.
		if (!mQuiltFB || !mSceneFB || !renderedViews || renderedCount <= 0) return;
		const int total = columns * rows;
		const int tw = mQuiltTileW;
		const int th = mQuiltTileH;
		for (int view = 0; view < total; ++view)
		{
			int best = renderedViews[0];
			int bestDist = abs(view - best);
			bool isRendered = false;
			for (int i = 0; i < renderedCount; ++i)
			{
				if (renderedViews[i] == view) { isRendered = true; break; }
				const int d = abs(view - renderedViews[i]);
				if (d < bestDist) { best = renderedViews[i]; bestDist = d; }
			}
			if (isRendered || best == view) continue;

			const int sx0 = (best % columns) * tw;
			const int sy0 = (best / columns) * th;
			const int dx0 = (view % columns) * tw;
			const int dy0 = (view / columns) * th;

			glBindFramebuffer(GL_READ_FRAMEBUFFER, mQuiltFB.handle);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mSceneFB.handle);
			glBlitFramebuffer(
				sx0, sy0, sx0 + tw, sy0 + th,
				0, 0, tw, th,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);

			glBindFramebuffer(GL_READ_FRAMEBUFFER, mSceneFB.handle);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mQuiltFB.handle);
			glBlitFramebuffer(
				0, 0, tw, th,
				dx0, dy0, dx0 + tw, dy0 + th,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		BindSceneFB(false);
	}

	void FGLRenderBuffers::BindLookingGlassQuiltTexture(int index, int filter, int wrap)
	{
		mQuiltTex.Bind(index, filter, wrap);
	}

}  // namespace OpenGLRenderer