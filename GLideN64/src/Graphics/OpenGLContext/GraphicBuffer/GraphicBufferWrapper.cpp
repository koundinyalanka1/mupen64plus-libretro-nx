// Based on https://github.com/chromium/chromium/blob/master/base/android/android_hardware_buffer_compat.h

#include "GraphicBufferWrapper.h"
#include <Graphics/OpenGLContext/GraphicBuffer/PublicApi/android_hardware_buffer_compat.h>
#include "../GLFunctions.h"

#include <cerrno>

namespace opengl {


GraphicBufferWrapper::GraphicBufferWrapper() {
	if (!isSupportAvailable()) {
		return;
	}

	if (AndroidHardwareBufferCompat::GetApiLevel() <= 23) {
		m_private = true;
		m_privateGraphicBuffer = new GraphicBuffer();
	}
}

GraphicBufferWrapper::~GraphicBufferWrapper() {
	release();
	if (m_private) {
		delete m_privateGraphicBuffer;
	}
}

bool GraphicBufferWrapper::isSupportAvailable() {

	const int apiLevel = AndroidHardwareBufferCompat::GetApiLevel();
	return isPublicSupportAvailable() ||
		(apiLevel > 0 && apiLevel <= 23 && GraphicBuffer::hasBufferMapper());
}

bool GraphicBufferWrapper::isPublicSupportAvailable() {

	return AndroidHardwareBufferCompat::IsSupportAvailable() &&
		IS_GL_FUNCTION_VALID(GetNativeClientBufferANDROID);
}

bool GraphicBufferWrapper::allocate(const AHardwareBuffer_Desc *desc) {

	if (m_private) {
		return m_privateGraphicBuffer->reallocate(desc->width, desc->height, desc->format, desc->usage);
	} else {
		if (!isPublicSupportAvailable())
			return false;
		release();
		auto& api = AndroidHardwareBufferCompat::GetInstance();
		if (api.Allocate(desc, &m_publicGraphicBuffer) != 0)
			return false;
		AHardwareBuffer_Desc allocatedDesc{};
		api.Describe(m_publicGraphicBuffer, &allocatedDesc);
		m_stride = allocatedDesc.stride;
		return true;
	}
}

int GraphicBufferWrapper::lock(uint64_t usage, void **out_virtual_address) {

	int returnValue = 0;
	if (m_private) {
		returnValue = m_privateGraphicBuffer->lock(usage, out_virtual_address) ? 0 : -EINVAL;
	} else {
		if (m_publicGraphicBuffer == nullptr)
			return -EINVAL;
		returnValue = AndroidHardwareBufferCompat::GetInstance().Lock(m_publicGraphicBuffer, usage, -1, nullptr, out_virtual_address);
	};

	return returnValue;
}

void GraphicBufferWrapper::release() {
	if (m_publicGraphicBuffer != nullptr) {
		AndroidHardwareBufferCompat::GetInstance().Release(m_publicGraphicBuffer);
		m_publicGraphicBuffer = nullptr;
		m_stride = 0;
	}
}

void GraphicBufferWrapper::unlock() {

	if (m_private) {
		m_privateGraphicBuffer->unlock();
	} else if (m_publicGraphicBuffer != nullptr) {
		AndroidHardwareBufferCompat::GetInstance().Unlock(m_publicGraphicBuffer, nullptr);
	}

}

EGLClientBuffer GraphicBufferWrapper::getClientBuffer() {
	EGLClientBuffer clientBuffer = nullptr;
	if (m_private) {
		clientBuffer = (EGLClientBuffer)m_privateGraphicBuffer->getNativeBuffer();
	} else if (m_publicGraphicBuffer != nullptr) {
		clientBuffer = eglGetNativeClientBufferANDROID(m_publicGraphicBuffer);
	}

	return clientBuffer;
}

unsigned GraphicBufferWrapper::getStride() const {
	if (m_private) {
		return m_privateGraphicBuffer->getStride();
	} else {
		return m_stride;
	}
}

}
