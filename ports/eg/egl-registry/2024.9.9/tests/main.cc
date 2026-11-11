// Headers only: nothing here calls into a libEGL, so nothing is linked.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <KHR/khrplatform.h>

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

// In C++, EGL_CAST is a static_cast, so the handle constants keep their
// types, and the prototypes declare these return types (decltype does not
// call them).
static_assert(std::is_same<decltype(EGL_NO_CONTEXT), EGLContext>::value &&
                  std::is_same<decltype(EGL_DONT_CARE), EGLint>::value &&
                  std::is_same<decltype(eglGetError()), EGLint>::value &&
                  std::is_same<decltype(eglGetProcAddress("")),
                               __eglMustCastToProperFunctionPointerType>::value,
              "EGL_CAST and the prototypes");
static_assert(sizeof(khronos_int32_t) == 4 && sizeof(khronos_int64_t) == 8 &&
                  sizeof(khronos_intptr_t) == sizeof(void *) &&
                  sizeof(EGLint) == 4 && sizeof(EGLAttrib) == sizeof(void *),
              "khronos and EGL type sizes");

int native_display;
EGLenum seen_platform;

EGLDisplay EGLAPIENTRY fake_get_platform_display(EGLenum platform,
                                                 void *native,
                                                 const EGLint *) {
  seen_platform = platform;
  return native == &native_display ? static_cast<EGLDisplay>(&native_display)
                                   : EGL_NO_DISPLAY;
}

// The way a loader uses eglGetProcAddress: a generic function pointer that
// the caller casts to the extension's PFN type.
__eglMustCastToProperFunctionPointerType EGLAPIENTRY
fake_get_proc_address(const char *name) {
  return std::strcmp(name, "eglGetPlatformDisplayEXT") == 0
             ? reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
                   fake_get_platform_display)
             : nullptr;
}

}  // namespace

int main() {
  check(static_cast<khronos_uint32_t>(-1) > 0 &&
            static_cast<khronos_int32_t>(-1) < 0 &&
            KHRONOS_SUPPORT_INT64 == 1 && KHRONOS_TRUE == 1,
        "khronos types");
  check(EGL_NO_DISPLAY == nullptr && EGL_NO_SYNC == nullptr &&
            EGL_NO_IMAGE_KHR == nullptr && EGL_DONT_CARE == -1,
        "EGL null handles and EGL_DONT_CARE");
  check(EGL_FOREVER == static_cast<EGLTime>(-1), "EGL_FOREVER");
  check(EGL_SUCCESS == 0x3000 && EGL_NONE == 0x3038 &&
            EGL_CONTEXT_MAJOR_VERSION == 0x3098 &&
            EGL_PLATFORM_GBM_KHR == 0x31D7,
        "EGL enumerants");
  check(EGL_VERSION_1_5 == 1 && EGL_EXT_platform_base == 1 &&
            EGL_EGLEXT_VERSION == 20240909,
        "EGL versions and extensions");

  PFNEGLGETPROCADDRESSPROC get_proc_address = fake_get_proc_address;
  const EGLint attribs[] = {EGL_PLATFORM_X11_SCREEN_KHR, 0, EGL_NONE};
  auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
      get_proc_address("eglGetPlatformDisplayEXT"));
  check(get_platform_display != nullptr &&
            get_platform_display(EGL_PLATFORM_GBM_KHR, &native_display,
                                 attribs) ==
                static_cast<EGLDisplay>(&native_display) &&
            seen_platform == EGL_PLATFORM_GBM_KHR,
        "an extension entry point through PFNEGLGETPLATFORMDISPLAYEXTPROC");
  check(get_proc_address("eglNoSuchFunction") == nullptr,
        "an unknown entry point");

  if (failures != 0) {
    return 1;
  }
  std::printf("egl-registry: EGL 1.5 headers, EGL_EGLEXT_VERSION %d\n",
              static_cast<int>(EGL_EGLEXT_VERSION));
  return 0;
}
