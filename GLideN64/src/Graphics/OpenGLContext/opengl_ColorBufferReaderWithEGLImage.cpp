#if defined(EGL) && defined(OS_ANDROID)

#include <GBI.h>
#include <Graphics/Context.h>
#include <libretro_private.h>
#include "opengl_ColorBufferReaderWithEGLImage.h"

using namespace opengl;
using namespace graphics;

ColorBufferReaderWithEGLImage::ColorBufferReaderWithEGLImage(CachedTexture *_pTexture, CachedBindTexture *_bindTexture)
	: graphics::ColorBufferReader(_pTexture)
	, m_bindTexture(_bindTexture)
	, m_image(nullptr)
	, m_usage(AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE)
	, m_bufferLocked(false)
{
	_initBuffers();
}

ColorBufferReaderWithEGLImage::~ColorBufferReaderWithEGLImage()
{
	cleanUp();

	if (m_image != nullptr) {
		eglDestroyImageKHR(eglGetDisplay(EGL_DEFAULT_DISPLAY), m_image);
	}
	m_hardwareBuffer.release();
}

void ColorBufferReaderWithEGLImage::_initBuffers()
{
	AHardwareBuffer_Desc bufferDesc{m_pTexture->width, m_pTexture->height,
		1, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
		m_usage,
		0,0};
	/* This texture never receives glTexImage2D: every byte of its storage
	 * arrives through the chain below. Each link is reported, because a
	 * failure anywhere here leaves the colour-buffer FBO incomplete for the
	 * whole session -- the completeness check in
	 * FrameBuffer::_initColorFBTexture is an assert, and so is compiled out
	 * of release builds. */
	if (!m_hardwareBuffer.allocate(&bufferDesc)) {
		if (log_cb)
			log_cb(RETRO_LOG_WARN,
				"GLideN64: AHardwareBuffer_allocate failed for %ux%u\n",
				m_pTexture->width, m_pTexture->height);
		return;
	}

	if(m_image == nullptr)
	{
		EGLint eglImgAttrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE, EGL_NONE };
		m_image = eglCreateImageKHR(eglGetDisplay(EGL_DEFAULT_DISPLAY), EGL_NO_CONTEXT,
			EGL_NATIVE_BUFFER_ANDROID, m_hardwareBuffer.getClientBuffer(), eglImgAttrs);

		if (m_image == nullptr) {
			if (log_cb)
				log_cb(RETRO_LOG_WARN,
					"GLideN64: eglCreateImageKHR failed (EGL error 0x%x)\n",
					(unsigned)eglGetError());
			return;
		}

		m_bindTexture->bind(graphics::Parameter(0), textureTarget::TEXTURE_EXTERNAL, m_pTexture->name);
		while (glGetError() != GL_NO_ERROR);
		glEGLImageTargetTexture2DOES(GLenum(textureTarget::TEXTURE_EXTERNAL), m_image);
		{
			const GLenum err = glGetError();
			if (log_cb)
				log_cb(err == GL_NO_ERROR ? RETRO_LOG_INFO : RETRO_LOG_WARN,
					"GLideN64: glEGLImageTargetTexture2DOES on texture %u -> GL 0x%x\n",
					(unsigned)m_pTexture->name, (unsigned)err);
		}
		m_bindTexture->bind(graphics::Parameter(0), textureTarget::TEXTURE_EXTERNAL, ObjectHandle());
	}
}


const u8 * ColorBufferReaderWithEGLImage::_readPixels(const ReadColorBufferParams& _params, u32& _heightOffset,
	u32& _stride)
{
	GLenum format = GLenum(_params.colorFormat);
	GLenum type = GLenum(_params.colorType);

	void* gpuData = nullptr;

	// Hardware buffers hold RGBA8; monochrome reads still need GL conversion.
	if (!_params.sync && _params.colorFormat == colorFormat::RGBA && _params.colorType == datatype::UNSIGNED_BYTE) {
		if (m_hardwareBuffer.lock(AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, &gpuData) != 0)
			return nullptr;
		m_bufferLocked = true;
		_heightOffset = static_cast<u32>(_params.y0);
		_stride = m_hardwareBuffer.getStride();
	} else {
		gpuData = m_pixelData.data();
		glReadPixels(_params.x0, _params.y0,  m_pTexture->width, _params.height, format, type, gpuData);
		_heightOffset = 0;
		_stride = m_pTexture->width;
	}

	return reinterpret_cast<u8*>(gpuData);
}

void ColorBufferReaderWithEGLImage::cleanUp()
{
	if (m_bufferLocked) {
		m_hardwareBuffer.unlock();
		m_bufferLocked = false;
	}
}

#endif // EGL
