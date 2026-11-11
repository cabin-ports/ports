/* Headers only: nothing here calls into a libEGL, so nothing is linked. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <KHR/khrplatform.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

/* Stand-ins with the signatures the PFN typedefs declare, used the way a
   loader is: eglGetProcAddress returns a generic function pointer that the
   caller casts to the extension's PFN type. */
static int native_display;
static EGLenum seen_platform;
static EGLint seen_first_attrib;

static EGLDisplay EGLAPIENTRY fake_get_platform_display(
    EGLenum platform, void *native, const EGLint *attrib_list) {
  seen_platform = platform;
  seen_first_attrib = attrib_list[0];
  return native == &native_display ? (EGLDisplay)&native_display
                                   : EGL_NO_DISPLAY;
}

static __eglMustCastToProperFunctionPointerType EGLAPIENTRY
fake_get_proc_address(const char *name) {
  return strcmp(name, "eglGetPlatformDisplayEXT") == 0
             ? (__eglMustCastToProperFunctionPointerType)
                   fake_get_platform_display
             : NULL;
}

static EGLint EGLAPIENTRY fake_get_error(void) { return EGL_SUCCESS; }

int main(void) {
  PFNEGLGETPROCADDRESSPROC get_proc_address = fake_get_proc_address;
  PFNEGLGETERRORPROC get_error = fake_get_error;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
  const EGLint attribs[] = {EGL_PLATFORM_X11_SCREEN_KHR, 0, EGL_NONE};
  EGLDisplay display;

  check(sizeof(khronos_int8_t) == 1 && sizeof(khronos_int16_t) == 2 &&
            sizeof(khronos_int32_t) == 4 && sizeof(khronos_int64_t) == 8,
        "khronos fixed-width types");
  check((khronos_int32_t)-1 < 0 && (khronos_uint32_t)-1 > 0 &&
            (khronos_uint64_t)-1 > 0,
        "khronos signedness");
  check(sizeof(khronos_intptr_t) == sizeof(void *) &&
            sizeof(khronos_uintptr_t) == sizeof(void *),
        "khronos pointer-sized integers");
  check(KHRONOS_SUPPORT_INT64 == 1 && KHRONOS_SUPPORT_FLOAT == 1 &&
            sizeof(khronos_float_t) == 4,
        "khronos 64-bit and float support");
  check(sizeof(khronos_boolean_enum_t) == 4 && KHRONOS_TRUE == 1 &&
            KHRONOS_FALSE == 0,
        "khronos_boolean_enum_t");

  check(sizeof(EGLint) == 4 && sizeof(EGLAttrib) == sizeof(void *) &&
            sizeof(EGLTime) == 8 && sizeof(EGLuint64KHR) == 8,
        "EGL scalar types");
  check(EGL_TRUE == 1 && EGL_FALSE == 0 && EGL_DONT_CARE == -1 &&
            EGL_UNKNOWN == -1,
        "EGL booleans and EGL_DONT_CARE");
  check(EGL_NO_DISPLAY == (EGLDisplay)0 && EGL_NO_CONTEXT == (EGLContext)0 &&
            EGL_NO_SYNC == (EGLSync)0 && EGL_NO_IMAGE_KHR == (EGLImageKHR)0 &&
            EGL_DEFAULT_DISPLAY == (EGLNativeDisplayType)0,
        "EGL null handles");
  check((EGLTime)EGL_FOREVER == (khronos_utime_nanoseconds_t)-1,
        "EGL_FOREVER");
  check(EGL_SUCCESS == 0x3000 && EGL_BAD_ACCESS == 0x3002 &&
            EGL_NONE == 0x3038 && EGL_CONTEXT_MAJOR_VERSION == 0x3098 &&
            EGL_OPENGL_ES3_BIT == 0x40 && EGL_PLATFORM_GBM_KHR == 0x31D7,
        "EGL enumerants");
  check(EGL_VERSION_1_5 == 1 && EGL_KHR_image_base == 1 &&
            EGL_EXT_platform_base == 1 && EGL_EGLEXT_VERSION == 20240909,
        "EGL versions and extensions");

  /* The prototypes declare these return types; sizeof does not call them. */
  check(sizeof(eglGetError()) == sizeof(EGLint) &&
            sizeof(eglGetDisplay(EGL_DEFAULT_DISPLAY)) == sizeof(EGLDisplay),
        "EGL prototypes");

  get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)get_proc_address(
      "eglGetPlatformDisplayEXT");
  display = get_platform_display != NULL
                ? get_platform_display(EGL_PLATFORM_GBM_KHR, &native_display,
                                       attribs)
                : EGL_NO_DISPLAY;
  check(display == (EGLDisplay)&native_display &&
            seen_platform == EGL_PLATFORM_GBM_KHR &&
            seen_first_attrib == EGL_PLATFORM_X11_SCREEN_KHR,
        "an extension entry point through PFNEGLGETPLATFORMDISPLAYEXTPROC");
  check(get_proc_address("eglNoSuchFunction") == NULL &&
            get_error() == EGL_SUCCESS,
        "PFNEGLGETPROCADDRESSPROC and PFNEGLGETERRORPROC");

  if (failures != 0) {
    return 1;
  }
  printf("egl-registry: EGL 1.5 headers, EGL_EGLEXT_VERSION %d\n",
         (int)EGL_EGLEXT_VERSION);
  return 0;
}
