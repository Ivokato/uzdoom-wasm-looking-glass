#pragma once

#include "hwrenderer/postprocessing/hw_postprocess.h"

namespace OpenGLESRenderer
{

class FGLRenderBuffers;

class PPGLTexture
{
public:
	void Bind(int index, int filter = GL_NEAREST, int wrap = GL_CLAMP_TO_EDGE)
	{
		glActiveTexture(GL_TEXTURE0 + index);
		glBindTexture(GL_TEXTURE_2D, handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	}

	int Width = -1;
	int Height = -1;

	explicit operator bool() const { return handle != 0; }

private:
	GLuint handle = 0;

	friend class FGLRenderBuffers;
	friend class PPGLTextureBackend;
};

class PPGLFrameBuffer
{
public:
	void Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, handle);
	}

	explicit operator bool() const { return handle != 0; }

private:
	GLuint handle = 0;

	friend class FGLRenderBuffers;
	friend class PPGLTextureBackend;
};

class PPGLRenderBuffer
{
private:
	GLuint handle = 0;

	explicit operator bool() const { return handle != 0; }

	friend class FGLRenderBuffers;
};

class PPGLTextureBackend : public PPTextureBackend
{
public:
	~PPGLTextureBackend()
	{
		if (Tex.handle != 0)
		{
			glDeleteTextures(1, &Tex.handle);
			Tex.handle = 0;
		}
		if (FB.handle != 0)
		{
			glDeleteFramebuffers(1, &FB.handle);
			FB.handle = 0;
		}
	}

	PPGLTexture Tex;
	PPGLFrameBuffer FB;
};

class FShaderProgram;


class FGLRenderBuffers
{
public:
	FGLRenderBuffers();
	~FGLRenderBuffers();

	void Setup(int width, int height, int sceneWidth, int sceneHeight);

	void BindSceneFB(bool sceneData);


	void BindCurrentTexture(int index, int filter = GL_NEAREST, int wrap = GL_CLAMP_TO_EDGE);
	void BindCurrentFB();
	void BindNextFB();
	void NextTexture();


	void BindOutputFB();

	void BindDitherTexture(int texunit);

	/** Looking Glass: ensure quilt FBO, clear, blit scene→tile, bind quilt for present. */
	void EnsureLookingGlassQuilt(int columns, int rows, int tileW, int tileH);
	void ClearLookingGlassQuilt();
	/** 1:1 (or uniform scale) scene → quilt tile. Scene must match tile aspect. */
	void BlitSceneToQuiltView(int viewIndex, int columns, int rows);
	void NearestFillLookingGlassQuilt(const int *renderedViews, int renderedCount, int columns, int rows);
	/** Stamp full scene (menus/console) onto every tile — flat UI. */
	void OverlaySceneOnAllLookingGlassTiles();
	/** Copy classic status-bar strip onto every tile (zero-parallax HUD). */
	void OverlayHudStripOnLookingGlassTiles();
	void BlitLookingGlassSceneToPanel(int panelW, int panelH);
	void BindLookingGlassQuiltTexture(int index, int filter = GL_LINEAR, int wrap = GL_CLAMP_TO_EDGE);
	bool HasLookingGlassQuilt() const { return mQuiltFB.handle != 0; }
	GLuint GetLookingGlassQuiltTextureName() const { return mQuiltTex.handle; }
	int GetQuiltWidth() const { return mQuiltWidth; }
	int GetQuiltHeight() const { return mQuiltHeight; }
	int GetQuiltColumns() const { return mQuiltColumns; }
	int GetQuiltRows() const { return mQuiltRows; }
	int GetQuiltTileW() const { return mQuiltTileW; }
	int GetQuiltTileH() const { return mQuiltTileH; }

	int GetWidth() const { return mWidth; }
	int GetHeight() const { return mHeight; }

	int GetSceneWidth() const { return mSceneWidth; }
	int GetSceneHeight() const { return mSceneHeight; }

private:
	void ClearScene();
	void ClearQuilt();

	void CreateScene(int width, int height);
	void CreatePipeline(int width, int height);

	PPGLTexture Create2DTexture(const char *name, GLuint format, int width, int height, const void *data = nullptr);
	PPGLRenderBuffer CreateRenderBuffer(const char *name, GLuint format, int width, int height);
	PPGLFrameBuffer CreateFrameBuffer(const char *name, PPGLTexture colorbuffer);
	PPGLFrameBuffer CreateFrameBuffer(const char *name, PPGLTexture colorbuffer, PPGLRenderBuffer depthstencil, PPGLRenderBuffer stencil);
	PPGLFrameBuffer CreateFrameBuffer(const char *name, PPGLRenderBuffer colorbuffer, PPGLRenderBuffer depthstencil, PPGLRenderBuffer stencil);
	PPGLFrameBuffer CreateFrameBuffer(const char *name, PPGLTexture colorbuffer0, PPGLTexture colorbuffer1, PPGLTexture colorbuffer2, PPGLTexture depthstencil, PPGLRenderBuffer stencil);
	bool CheckFrameBufferCompleteness();
	void ClearFrameBuffer(bool stencil, bool depth);
	void DeleteTexture(PPGLTexture &handle);
	void DeleteRenderBuffer(PPGLRenderBuffer &handle);
	void DeleteFrameBuffer(PPGLFrameBuffer &handle);

	int mWidth = 0;
	int mHeight = 0;
	int mSceneWidth = 0;
	int mSceneHeight = 0;

	// Buffers for the scene
	PPGLTexture mSceneDepthStencilTex;
	PPGLTexture mSceneTex;
	PPGLRenderBuffer mSceneDepthStencilBuf;
	PPGLRenderBuffer mSceneStencilBuf; // This is only use when combined depth-stencil is not avaliable
	PPGLFrameBuffer mSceneFB;
	bool mSceneUsesTextures = false;

	PPGLTexture mDitherTexture;

	// Looking Glass quilt (packed N-view color target)
	PPGLTexture mQuiltTex;
	PPGLFrameBuffer mQuiltFB;
	int mQuiltWidth = 0;
	int mQuiltHeight = 0;
	int mQuiltColumns = 0;
	int mQuiltRows = 0;
	int mQuiltTileW = 0;
	int mQuiltTileH = 0;

	static bool FailedCreate;

	friend class GLPPRenderState;
};

}
