// Based on https://github.com/chromium/chromium/blob/master/base/android/android_hardware_buffer_compat.h

#include "android_hardware_buffer_compat.h"

#include <dlfcn.h>
#include <sys/system_properties.h>
#include <cstdlib>

namespace opengl {


AndroidHardwareBufferCompat::AndroidHardwareBufferCompat() {
    // TODO(klausw): If the Chromium build requires __ANDROID_API__ >= 26 at some
    // point in the future, we could directly use the global functions instead of
    // dynamic loading. However, since this would be incompatible with pre-Oreo
    // devices, this is unlikely to happen in the foreseeable future, so just
    // unconditionally use dynamic loading.

    // Load the owning library explicitly; it need not be globally loaded by
    // the frontend. Keep it alive for as long as these function pointers live.
    library_ = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (library_ == nullptr)
        return;

    *reinterpret_cast<void **>(&allocate_) =
            dlsym(library_, "AHardwareBuffer_allocate");
    DCHECK(allocate_);

    *reinterpret_cast<void **>(&acquire_) =
            dlsym(library_, "AHardwareBuffer_acquire");
    DCHECK(acquire_);

    *reinterpret_cast<void **>(&describe_) =
            dlsym(library_, "AHardwareBuffer_describe");
    DCHECK(describe_);

    *reinterpret_cast<void **>(&lock_) =
            dlsym(library_, "AHardwareBuffer_lock");
    DCHECK(lock_);

    *reinterpret_cast<void **>(&recv_handle_) =
            dlsym(library_, "AHardwareBuffer_recvHandleFromUnixSocket");
    DCHECK(recv_handle_);

    *reinterpret_cast<void **>(&release_) =
            dlsym(library_, "AHardwareBuffer_release");
    DCHECK(release_);

    *reinterpret_cast<void **>(&send_handle_) =
            dlsym(library_, "AHardwareBuffer_sendHandleToUnixSocket");
    DCHECK(send_handle_);

    *reinterpret_cast<void **>(&unlock_) =
            dlsym(library_, "AHardwareBuffer_unlock");
    DCHECK(unlock_);
}

AndroidHardwareBufferCompat::~AndroidHardwareBufferCompat() {
    if (library_ != nullptr)
        dlclose(library_);
}

int AndroidHardwareBufferCompat::GetApiLevel() {
    // C++11 initializes this once, even if renderer threads race on first use.
    static const int apiLevel = []() {
        char value[PROP_VALUE_MAX] = {};
        return __system_property_get("ro.build.version.sdk", value) > 0
            ? std::atoi(value) : 0;
    }();
    return apiLevel;
}

bool AndroidHardwareBufferCompat::IsSupportAvailable() {
    if (GetApiLevel() < 26)
        return false;
    const auto& compat = GetInstance();
    return compat.allocate_ && compat.acquire_ && compat.describe_ &&
        compat.lock_ && compat.recv_handle_ && compat.release_ &&
        compat.send_handle_ && compat.unlock_;
}


AndroidHardwareBufferCompat &AndroidHardwareBufferCompat::GetInstance() {
    static AndroidHardwareBufferCompat compat;
    return compat;
}

int AndroidHardwareBufferCompat::Allocate(const AHardwareBuffer_Desc *desc,
                                           AHardwareBuffer **out_buffer) {
    DCHECK(IsSupportAvailable());
    return allocate_(desc, out_buffer);
}

void AndroidHardwareBufferCompat::Acquire(AHardwareBuffer *buffer) {
    DCHECK(IsSupportAvailable());
    acquire_(buffer);
}

void AndroidHardwareBufferCompat::Describe(const AHardwareBuffer *buffer,
                                           AHardwareBuffer_Desc *out_desc) {
    DCHECK(IsSupportAvailable());
    describe_(buffer, out_desc);
}

int AndroidHardwareBufferCompat::Lock(AHardwareBuffer *buffer,
                                      uint64_t usage,
                                      int32_t fence,
                                      const ARect *rect,
                                      void **out_virtual_address) {
    DCHECK(IsSupportAvailable());
    return lock_(buffer, usage, fence, rect, out_virtual_address);
}

int AndroidHardwareBufferCompat::RecvHandleFromUnixSocket(
        int socket_fd,
        AHardwareBuffer **out_buffer) {
    DCHECK(IsSupportAvailable());
    return recv_handle_(socket_fd, out_buffer);
}

void AndroidHardwareBufferCompat::Release(AHardwareBuffer *buffer) {
    DCHECK(IsSupportAvailable());
    release_(buffer);
}

int AndroidHardwareBufferCompat::SendHandleToUnixSocket(
        const AHardwareBuffer *buffer,
        int socket_fd) {
    DCHECK(IsSupportAvailable());
    return send_handle_(buffer, socket_fd);
}

int AndroidHardwareBufferCompat::Unlock(AHardwareBuffer *buffer,
                                        int32_t *fence) {
    DCHECK(IsSupportAvailable());
    return unlock_(buffer, fence);
}

}  // namespace base