// Exercise the actual wrapper/compatibility/Utils sources without an Android GPU.
// CHECK stays active with NDEBUG, so this covers release-build framebuffer checks.
#include <Graphics/OpenGLContext/GraphicBuffer/GraphicBufferWrapper.h>
#include <Graphics/OpenGLContext/GraphicBuffer/PublicApi/android_hardware_buffer_compat.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#undef CHECK
#define CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::abort(); \
} } while (0)

static const char* scenario;
static int propertyQueries, allocations, releases, descriptions, unlocks;
static bool allocationFails, lockFails;
static AHardwareBuffer storage{};
static GLenum framebufferStatus = GL_FRAMEBUFFER_COMPLETE;

static void* nativeBuffer(const AHardwareBuffer* buffer) { return (void*)buffer; }
void* (*ptrGetNativeClientBufferANDROID)(const AHardwareBuffer*) = nativeBuffer;

extern "C" int __system_property_get(const char*, char* value)
{
    ++propertyQueries;
    if (std::strcmp(scenario, "missing-property") == 0)
        return 0;
    std::strcpy(value, std::strcmp(scenario, "old-api") == 0 ? "23" : "35");
    return 2;
}

static int allocate(const AHardwareBuffer_Desc*, AHardwareBuffer** buffer)
{
    if (allocationFails)
        return -1;
    ++allocations;
    *buffer = &storage;
    return 0;
}
static void release(AHardwareBuffer* buffer) { CHECK(buffer == &storage); ++releases; }
static void describe(const AHardwareBuffer*, AHardwareBuffer_Desc* desc)
{
    ++descriptions;
    desc->stride = 128;
}
static int lock(AHardwareBuffer*, uint64_t, int32_t, const ARect*, void** data)
{
    if (lockFails)
        return -1;
    *data = &storage;
    return 0;
}
static int unlock(AHardwareBuffer*, int32_t*) { ++unlocks; return 0; }
static void unused() {}

extern "C" void* test_dlopen(const char* name, int)
{
    CHECK(name && std::strcmp(name, "libandroid.so") == 0);
    return std::strcmp(scenario, "missing-library") == 0 ? nullptr : &storage;
}
extern "C" int test_dlclose(void*) { return 0; }
extern "C" void* test_dlsym(void*, const char* name)
{
    if (std::strcmp(scenario, "missing-symbol") == 0 &&
        std::strcmp(name, "AHardwareBuffer_lock") == 0)
        return nullptr;
    if (std::strcmp(name, "AHardwareBuffer_allocate") == 0) return (void*)allocate;
    if (std::strcmp(name, "AHardwareBuffer_release") == 0) return (void*)release;
    if (std::strcmp(name, "AHardwareBuffer_describe") == 0) return (void*)describe;
    if (std::strcmp(name, "AHardwareBuffer_lock") == 0) return (void*)lock;
    if (std::strcmp(name, "AHardwareBuffer_unlock") == 0) return (void*)unlock;
    return (void*)unused;
}

// This fork has no private gralloc allocator; old devices must fall back safely.
namespace opengl {
GraphicBuffer::GraphicBuffer() {}
GraphicBuffer::~GraphicBuffer() {}
bool GraphicBuffer::hasBufferMapper() { return false; }
bool GraphicBuffer::reallocate(unsigned, unsigned, unsigned, unsigned) { return false; }
bool GraphicBuffer::lock(unsigned, void**) { return false; }
void GraphicBuffer::unlock() {}
unsigned GraphicBuffer::getStride() { return 0; }
android_native_buffer_t* GraphicBuffer::getNativeBuffer() { return nullptr; }
}

extern "C" GLenum glCheckFramebufferStatus(GLenum) { return framebufferStatus; }
extern "C" GLenum glGetError() { return GL_NO_ERROR; }
extern "C" void glGetIntegerv(GLenum, GLint* value) { *value = 0; }
extern "C" const GLubyte* glGetString(GLenum) { return (const GLubyte*)""; }
extern "C" const GLubyte* glGetStringi(GLenum, GLuint) { return nullptr; }

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    scenario = argv[1];
    CHECK(!opengl::Utils::isFramebufferError());
    framebufferStatus = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    CHECK(opengl::Utils::isFramebufferError());

    if (std::strcmp(scenario, "missing-egl") == 0)
        ptrGetNativeClientBufferANDROID = nullptr;
    const bool supported = std::strcmp(scenario, "success") == 0;
    for (int i = 0; i < 100; ++i)
        CHECK(opengl::GraphicBufferWrapper::isSupportAvailable() == supported);
    CHECK(propertyQueries == 1);

    AHardwareBuffer_Desc desc{};
    {
        opengl::GraphicBufferWrapper buffer;
        buffer.release(); // Safe even if allocation was never attempted.
        if (!supported) {
            CHECK(!buffer.allocate(&desc));
            CHECK(buffer.getClientBuffer() == nullptr);
            CHECK(buffer.getStride() == 0);
            return 0;
        }
        allocationFails = true;
        CHECK(!buffer.allocate(&desc));
        buffer.release();
        allocationFails = false;
        CHECK(buffer.allocate(&desc));
        CHECK(buffer.getClientBuffer() == &storage);
        for (int i = 0; i < 100; ++i)
            CHECK(buffer.getStride() == 128);
        CHECK(descriptions == 1);
        void* data = nullptr;
        lockFails = true;
        CHECK(buffer.lock(0, &data) != 0);
        lockFails = false;
        CHECK(buffer.lock(0, &data) == 0 && data == &storage);
        buffer.unlock();
        CHECK(unlocks == 1);
        CHECK(buffer.allocate(&desc)); // Reallocation releases the previous buffer.
        CHECK(releases == 1);
        buffer.release();
        buffer.release();
        CHECK(releases == 2);
        CHECK(buffer.getStride() == 0);
        CHECK(buffer.allocate(&desc)); // Destructor owns final cleanup.
    }
    CHECK(allocations == 3 && releases == 3);
    CHECK(propertyQueries == 1);
}
