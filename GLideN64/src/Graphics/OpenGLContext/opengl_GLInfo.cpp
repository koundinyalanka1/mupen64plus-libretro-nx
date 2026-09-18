#include <Log.h>
#include <Config.h>
#include "opengl_Utils.h"
#include "opengl_GLInfo.h"
#include <regex>
#include <Graphics/Parameters.h>

#ifdef EGL
#include <EGL/egl.h>
#endif

#ifdef OS_ANDROID
#include <Graphics/OpenGLContext/GraphicBuffer/GraphicBufferWrapper.h>
#endif

#if defined(EGL) && defined(OS_ANDROID)
#include <libretro_private.h>
#ifndef GL_TEXTURE_BINDING_EXTERNAL_OES
#define GL_TEXTURE_BINDING_EXTERNAL_OES 0x8D67
#endif
#endif

using namespace opengl;

#if defined(EGL) && defined(OS_ANDROID)
/* Ask the driver whether an EGLImage-backed external texture can actually be
 * used as a colour attachment, instead of inferring it from extension strings.
 *
 * This matters because FrameBuffer::_initColorFBTexture deliberately skips
 * glTexImage2D for these textures -- their storage is supposed to arrive over
 * the AHardwareBuffer -> EGLImage -> GL_TEXTURE_EXTERNAL_OES chain. On drivers
 * where external textures are sampling-only that chain never yields a
 * colour-renderable image, so every colour-buffer FBO is left permanently
 * GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT and each frame's blit and readback is
 * silently rejected. Probing here lets those drivers fall back to the ordinary
 * TEXTURE_2D path, which does call init2DTexture and has real storage.
 *
 * Runs once, during ContextImpl::init(), before anything reads the flag. */
static bool _probeEglImageColorAttachment()
{
	GraphicBufferWrapper buffer;
	AHardwareBuffer_Desc desc{ 64, 64, 1, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
		AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
		0, 0 };

	if (!buffer.allocate(&desc)) {
		if (log_cb)
			log_cb(RETRO_LOG_WARN, "GLideN64: EGLImage probe: buffer allocation failed\n");
		return false;
	}

	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint eglImgAttrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE, EGL_NONE };
	EGLImageKHR image = eglCreateImageKHR(display, EGL_NO_CONTEXT,
		EGL_NATIVE_BUFFER_ANDROID, buffer.getClientBuffer(), eglImgAttrs);

	if (image == nullptr) {
		if (log_cb)
			log_cb(RETRO_LOG_WARN, "GLideN64: EGLImage probe: eglCreateImageKHR failed (EGL error 0x%x)\n",
				(unsigned)eglGetError());
		buffer.release();
		return false;
	}

	GLint prevFramebuffer = 0;
	GLint prevReadFramebuffer = 0;
	GLint prevTexture = 0;
	const GLenum target = GLenum(graphics::textureTarget::TEXTURE_EXTERNAL);
	GLuint texture = 0;
	GLuint framebuffer = 0;
	GLenum bindError = GL_NO_ERROR;
	GLenum status = 0;
	bool usable = false;

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFramebuffer);
	glGetIntegerv(target == GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_EXTERNAL_OES, &prevTexture);

	while (glGetError() != GL_NO_ERROR);

	glGenTextures(1, &texture);
	glBindTexture(target, texture);
	glEGLImageTargetTexture2DOES(target, image);
	bindError = glGetError();

	if (bindError == GL_NO_ERROR) {
		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			target, texture, 0);
		status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		usable = (status == GL_FRAMEBUFFER_COMPLETE);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
	glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFramebuffer));
	if (framebuffer != 0)
		glDeleteFramebuffers(1, &framebuffer);
	glBindTexture(target, static_cast<GLuint>(prevTexture));
	glDeleteTextures(1, &texture);
	eglDestroyImageKHR(display, image);
	buffer.release();
	while (glGetError() != GL_NO_ERROR);

	if (log_cb)
		log_cb(usable ? RETRO_LOG_INFO : RETRO_LOG_WARN,
			"GLideN64: EGLImage colour-attachment probe: %s "
			"(bind GL 0x%x, framebuffer status 0x%x)\n",
			usable ? "usable" : "UNUSABLE, falling back to TEXTURE_2D",
			(unsigned)bindError, (unsigned)status);

	return usable;
}
#endif // EGL && OS_ANDROID

static
void APIENTRY on_gl_error(GLenum source,
						GLenum type,
						GLuint id,
						GLenum severity,
						GLsizei length,
						const char* message,
						const void *userParam)
{
	LOG(LOG_ERROR, "%s", message);
}

void GLInfo::init() {
	/* glGetString is allowed to return NULL. Feeding that to strstr/std::string
	 * below is undefined behaviour, so fall back to an empty string: every
	 * capability check then takes its conservative branch. */
	static const char * const strUnknown = "";
	const char * strDriverVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	if (strDriverVersion == nullptr) {
		LOG(LOG_WARNING, "Could not query GL_VERSION on this device");
		strDriverVersion = strUnknown;
	}
	isGLESX = strstr(strDriverVersion, "OpenGL ES") != nullptr;
	isGLES2 = strstr(strDriverVersion, "OpenGL ES 2") != nullptr;
	if (isGLES2) {
		majorVersion = 2;
		minorVersion = 0;
	} else {
		glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
		glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
	}

#if defined(HAVE_OPENGLES2) // Overwrite
	isGLES2 = true;
	majorVersion = 2;
	minorVersion = 0;
#endif

	LOG(LOG_VERBOSE, "%s major version: %d", isGLESX ? "OpenGL ES" : "OpenGL", majorVersion);
	LOG(LOG_VERBOSE, "%s minor version: %d", isGLESX ? "OpenGL ES" : "OpenGL", minorVersion);


	LOG(LOG_VERBOSE, "OpenGL vendor: %s", glGetString(GL_VENDOR));
	const char * strRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	if (strRenderer == nullptr) {
		LOG(LOG_WARNING, "Could not query GL_RENDERER on this device");
		strRenderer = strUnknown;
	}
	renderer = Renderer::Other;

	bool isAnyAdreno = strstr(strRenderer, "Adreno") != nullptr;
	bool isFreedreno = strstr(strRenderer, "FD") != nullptr;  // freedreno uses "FDxxx" naming
	bool isAdrenoFamily = isAnyAdreno || isFreedreno;

	if (std::regex_match(std::string(strRenderer), std::regex("Adreno.*530")))
		renderer = Renderer::Adreno530;
	else if (std::regex_match(std::string(strRenderer), std::regex("Adreno.*540")) ||
		std::regex_match(std::string(strRenderer), std::regex("Adreno.*6\\d\\d")) ||
		std::regex_match(std::string(strRenderer), std::regex(".*FD6\\d\\d.*")))  // freedreno 6xx series
		renderer = Renderer::Adreno_no_bugs;
	else if (std::regex_match(std::string(strRenderer), std::regex(".*FD5\\d\\d.*")))  // freedreno 5xx series
		renderer = Renderer::Adreno;
	else if (isAdrenoFamily)
		renderer = Renderer::Adreno;
	else if (strstr(strRenderer, "VideoCore IV") != nullptr)
		renderer = Renderer::VideoCore;
	else if (strstr(strRenderer, "Intel") != nullptr)
		renderer = Renderer::Intel;
	else if (strstr(strRenderer, "PowerVR") != nullptr)
		renderer = Renderer::PowerVR;
	else if (strstr(strRenderer, "NVIDIA Tegra") != nullptr)
		renderer = Renderer::Tegra;
	LOG(LOG_VERBOSE, "OpenGL renderer: %s", strRenderer);

	if (strstr(strDriverVersion, "ANGLE") != nullptr)
		renderer = Renderer::Angle;

	int numericVersion = majorVersion * 10 + minorVersion;
	if (isGLES2) {
		imageTextures = false;
		msaa = false;
	} else if (isGLESX) {
		imageTextures = (numericVersion >= 31);
		msaa = numericVersion >= 31;
	} else {
		imageTextures = (numericVersion >= 42) || Utils::isExtensionSupported(*this, "GL_ARB_shader_image_load_store");
		msaa = true;
	}

	//Tegra has a buggy implementation of fragment_shader_interlock that causes graphics lockups on drivers below 390.00
	bool hasBuggyFragmentShaderInterlock = false;

#ifndef OS_ANDROID
	if (renderer == Renderer::Tegra) {
		std::string strDriverVersionString(strDriverVersion);
		std::string nvidiaText = "NVIDIA";
		std::size_t versionPosition = strDriverVersionString.find(nvidiaText);

		if (versionPosition == std::string::npos) {
			hasBuggyFragmentShaderInterlock = true;
		} else {
			std::string strDriverVersionNumber = strDriverVersionString.substr(versionPosition + nvidiaText.length() + 1);
			float versionNumber = std::stof(strDriverVersionNumber);
			hasBuggyFragmentShaderInterlock = versionNumber < 390.0;
		}
	}
#endif

	fragment_interlock = Utils::isExtensionSupported(*this, "GL_ARB_fragment_shader_interlock") && !hasBuggyFragmentShaderInterlock;
	fragment_interlockNV = Utils::isExtensionSupported(*this, "GL_NV_fragment_shader_interlock") && !fragment_interlock && !hasBuggyFragmentShaderInterlock;
	fragment_ordering = Utils::isExtensionSupported(*this, "GL_INTEL_fragment_shader_ordering") && !fragment_interlock && !fragment_interlockNV;

	const bool imageTexturesInterlock = imageTextures && (fragment_interlock || fragment_interlockNV || fragment_ordering);

	if (isGLES2) {
		config.generalEmulation.enableFragmentDepthWrite = 0;
		config.generalEmulation.enableHybridFilter = 0;
	}

	// Force software vertex clipping to enabled for GLES
	if (isGLESX) {
		config.generalEmulation.enableClipping = 1;
	}

	drawElementsBaseVertex = !isGLESX ||
		(Utils::isExtensionSupported(*this, "GL_EXT_draw_elements_base_vertex") || numericVersion >= 32);
#ifdef EGL
	if (isGLESX && Utils::isExtensionSupported(*this, "GL_EXT_draw_elements_base_vertex") && numericVersion < 32) {
		ptrDrawRangeElementsBaseVertex = (PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC) eglGetProcAddress("glDrawRangeElementsBaseVertexEXT");
	}
#endif

	bufferStorage = (!isGLESX && (numericVersion >= 44)) || Utils::isExtensionSupported(*this, "GL_ARB_buffer_storage") ||
			Utils::isExtensionSupported(*this, "GL_EXT_buffer_storage");

	texStorage = (isGLESX && (numericVersion >= 30)) || (!isGLESX && numericVersion >= 42) ||
			Utils::isExtensionSupported(*this, "GL_ARB_texture_storage");

	shaderStorage = false;
	if (config.generalEmulation.enableShadersStorage != 0) {
		const char * strGetProgramBinary = isGLESX
			? "GL_OES_get_program_binary"
			: "GL_ARB_get_program_binary";
		if ((isGLESX && numericVersion >= 30) || (!isGLESX && numericVersion >= 41) || Utils::isExtensionSupported(*this, strGetProgramBinary)) {
			GLint numBinaryFormats = 0;
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numBinaryFormats);
			shaderStorage = numBinaryFormats > 0;
		}
	}

	bool ext_draw_buffers_indexed = isGLESX && (Utils::isExtensionSupported(*this, "GL_EXT_draw_buffers_indexed") || numericVersion >= 32);
#ifdef EGL
	if (isGLESX && bufferStorage)
		ptrBufferStorage = (PFNGLBUFFERSTORAGEPROC) eglGetProcAddress("glBufferStorageEXT");
	if (isGLESX && numericVersion < 32) {
		if (ext_draw_buffers_indexed) {
			ptrEnablei = (PFNGLENABLEIPROC) eglGetProcAddress("glEnableiEXT");
			ptrDisablei = (PFNGLDISABLEIPROC) eglGetProcAddress("glDisableiEXT");
		} else {
			ptrEnablei = nullptr;
			ptrDisablei = nullptr;
		}
	}
	if (isGLES2 && shaderStorage) {
		ptrProgramBinary = (PFNGLPROGRAMBINARYPROC) eglGetProcAddress("glProgramBinaryOES");
		ptrGetProgramBinary = (PFNGLGETPROGRAMBINARYPROC) eglGetProcAddress("glGetProgramBinaryOES");
		ptrProgramParameteri = nullptr;
	}
#endif
#ifndef OS_ANDROID
	if (isGLES2 && config.frameBufferEmulation.copyToRDRAM > Config::ctSync) {
		config.frameBufferEmulation.copyToRDRAM = Config::ctDisable;
		LOG(LOG_WARNING, "Async color buffer copies are not supported on GLES2");
	}
#endif
	if (isGLES2 && config.generalEmulation.enableLOD) {
		if (!Utils::isExtensionSupported(*this, "GL_EXT_shader_texture_lod") || !Utils::isExtensionSupported(*this, "GL_OES_standard_derivatives")) {
			config.generalEmulation.enableLOD = 0;
			LOG(LOG_WARNING, "LOD emulation not possible on this device");
		}
	}

	if (renderer == Renderer::PowerVR) {
		config.frameBufferEmulation.forceDepthBufferClear = 1;
		config.generalEmulation.enableFragmentDepthWrite = 0;
	}

#ifdef OS_ANDROID
	if (renderer == Renderer::Angle) {
        config.generalEmulation.enableFragmentDepthWrite = 0;
    }
#endif

	depthTexture = !isGLES2 || Utils::isExtensionSupported(*this, "GL_OES_depth_texture");
	noPerspective = Utils::isExtensionSupported(*this, "GL_NV_shader_noperspective_interpolation");

	fetch_depth = Utils::isExtensionSupported(*this, "GL_ARM_shader_framebuffer_fetch_depth_stencil");
	texture_barrier = !isGLESX && (numericVersion >= 45 || Utils::isExtensionSupported(*this, "GL_ARB_texture_barrier"));
	texture_barrierNV = Utils::isExtensionSupported(*this, "GL_NV_texture_barrier");

	ext_fetch = Utils::isExtensionSupported(*this, "GL_EXT_shader_framebuffer_fetch") && (!isGLESX || ext_draw_buffers_indexed);
	n64DepthWithFbFetch = ext_fetch && !imageTexturesInterlock;
	// The only EGLImage reader is compiled for Android with EGL support.
	eglImage = false;
	eglImageFramebuffer = false;
	ext_fetch_arm =  Utils::isExtensionSupported(*this, "GL_ARM_shader_framebuffer_fetch") && !ext_fetch;

	// Disable broken extensions if dual_source_blending is disabled (which is currently buggy with some settings)
	dual_source_blending = false; //!isGLESX || ((!isGLES2) && (Utils::isExtensionSupported(*this, "GL_EXT_blend_func_extended") && !isAnyAdreno));
	if (!dual_source_blending) {
		ext_fetch = false;
		ext_fetch_arm = false;
		n64DepthWithFbFetch = false;
	}

	anisotropic_filtering = Utils::isExtensionSupported(*this, "GL_EXT_texture_filter_anisotropic");

#if defined(EGL) && defined(OS_ANDROID)
	eglImage = (Utils::isEGLExtensionSupported("EGL_KHR_image_base") || Utils::isEGLExtensionSupported("EGL_KHR_image")) &&
		Utils::isEGLExtensionSupported("EGL_ANDROID_image_native_buffer") &&
		IS_GL_FUNCTION_VALID(EGLImageTargetTexture2DOES) &&
	        ( (isGLES2 && GraphicBufferWrapper::isSupportAvailable()) || (isGLESX && GraphicBufferWrapper::isPublicSupportAvailable()) );
	/* No vendor blocklist here on purpose. Whether an EGLImage-backed external
	 * texture is usable as a colour attachment is measured directly by
	 * _probeEglImageColorAttachment() below, so drivers that work are not
	 * excluded by name and drivers that do not fall back on their own. */
#endif

	// Reset this on every context initialization, including driver switches.
	graphics::textureTarget::TEXTURE_EXTERNAL = renderer == Renderer::Intel ? GL_TEXTURE_2D : GL_TEXTURE_EXTERNAL_OES;

	eglImageFramebuffer = eglImage && !isGLES2;

#if defined(EGL) && defined(OS_ANDROID)
	/* Extension strings only say the entry points exist, not that the result
	 * is colour-renderable. Confirm it before committing to the path. */
	if (eglImageFramebuffer)
		eglImageFramebuffer = _probeEglImageColorAttachment();

	/* The colour-buffer texture skips init2DTexture whenever EglImage is set,
	 * so the two flags must not disagree: without a renderable EGLImage the
	 * texture would be left with no storage at all. */
	if (!eglImageFramebuffer)
		eglImage = false;
#endif

#ifdef WEBOS
	eglImage = false;
	eglImageFramebuffer = false;
#endif

	if (config.frameBufferEmulation.N64DepthCompare != Config::dcDisable) {
		if (config.frameBufferEmulation.N64DepthCompare == Config::dcFast) {
			if (!imageTexturesInterlock && !n64DepthWithFbFetch) {
				config.frameBufferEmulation.N64DepthCompare = Config::dcDisable;
				LOG(LOG_WARNING, "Your GPU does not support the extensions needed for fast N64 Depth Compare.");
			}
		} else {
			// Compatible
			if (!imageTextures) {
				config.frameBufferEmulation.N64DepthCompare = Config::dcDisable;
				LOG(LOG_WARNING, "Your GPU does not support the extensions needed for N64 Depth Compare.");
			}
		}
	}

	coverage = dual_source_blending || ext_fetch || ext_fetch_arm;
	if (coverage) {
		GLint maxVertexAttribs = 0;
		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
		coverage = maxVertexAttribs >= 10;
	}

#ifdef EGL
	if (isGLESX)
	{
		ptrDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC) eglGetProcAddress("glDebugMessageCallbackKHR");
		ptrDebugMessageControl = (PFNGLDEBUGMESSAGECONTROLPROC) eglGetProcAddress("glDebugMessageControlKHR");
	}
#endif



#ifdef GL_DEBUG
	glDebugMessageCallback(on_gl_error, nullptr);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
}
