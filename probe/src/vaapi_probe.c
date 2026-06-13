#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef void* VADisplay;
typedef int VAStatus;
typedef int VAProfile;
typedef int VAEntrypoint;
typedef unsigned int VAConfigID;
typedef unsigned int VAContextID;
typedef unsigned int VASurfaceID;
typedef unsigned int VABufferID;
typedef int VAConfigAttribType;

typedef struct {
  VAConfigAttribType type;
  unsigned int value;
} VAConfigAttrib;

enum {
  VA_STATUS_SUCCESS = 0,
  VA_STATUS_ERROR_UNKNOWN = 0xFFFFFFFF,
  VA_PROFILE_H264_MAIN = 6,
  VA_PROFILE_H264_HIGH = 7,
  VA_PROFILE_H264_CONSTRAINED_BASELINE = 13,
  VA_PROFILE_HEVC_MAIN = 17,
  VA_PROFILE_HEVC_MAIN10 = 18,
  VA_PROFILE_HEVC_MAIN12 = 23,
  VA_PROFILE_HEVC_MAIN422_10 = 24,
  VA_PROFILE_HEVC_MAIN422_12 = 25,
  VA_PROFILE_HEVC_MAIN444 = 26,
  VA_PROFILE_HEVC_MAIN444_10 = 27,
  VA_PROFILE_HEVC_MAIN444_12 = 28,
  VA_ENTRYPOINT_VLD = 1,
};

static FILE* g_log;
static bool g_inject_profiles;
static bool g_force_vld;
static bool g_log_all_profiles;
static int g_profile_capacity_hint;
static int g_entrypoint_capacity_hint;
static __thread bool g_in_dlsym_hook;

typedef void* (*DlsymFn)(void*, const char*);
typedef VAStatus (*VaQueryConfigProfilesFn)(VADisplay, VAProfile*, int*);
typedef VAStatus (*VaQueryConfigEntrypointsFn)(VADisplay,
                                               VAProfile,
                                               VAEntrypoint*,
                                               int*);
typedef VAStatus (*VaCreateConfigFn)(VADisplay,
                                     VAProfile,
                                     VAEntrypoint,
                                     VAConfigAttrib*,
                                     int,
                                     VAConfigID*);
typedef VAStatus (*VaGetConfigAttributesFn)(VADisplay,
                                            VAProfile,
                                            VAEntrypoint,
                                            VAConfigAttrib*,
                                            int);
typedef int (*VaMaxNumProfilesFn)(VADisplay);
typedef int (*VaMaxNumEntrypointsFn)(VADisplay);
typedef const char* (*VaQueryVendorStringFn)(VADisplay);
typedef VAStatus (*VaCreateSurfacesFn)(VADisplay,
                                       unsigned int,
                                       unsigned int,
                                       unsigned int,
                                       VASurfaceID*,
                                       unsigned int,
                                       void*,
                                       unsigned int);
typedef VAStatus (*VaCreateContextFn)(VADisplay,
                                      VAConfigID,
                                      int,
                                      int,
                                      int,
                                      VASurfaceID*,
                                      int,
                                      VAContextID*);
typedef VAStatus (*VaCreateBufferFn)(VADisplay,
                                     VAContextID,
                                     int,
                                     unsigned int,
                                     unsigned int,
                                     void*,
                                     VABufferID*);
typedef VAStatus (*VaMapBufferFn)(VADisplay, VABufferID, void**);
typedef VAStatus (*VaUnmapBufferFn)(VADisplay, VABufferID);
typedef VAStatus (*VaBeginPictureFn)(VADisplay, VAContextID, VASurfaceID);
typedef VAStatus (*VaRenderPictureFn)(VADisplay, VAContextID, VABufferID*, int);
typedef VAStatus (*VaEndPictureFn)(VADisplay, VAContextID);
typedef VAStatus (*VaSyncSurfaceFn)(VADisplay, VASurfaceID);
typedef VAStatus (*VaDestroyBufferFn)(VADisplay, VABufferID);
typedef VAStatus (*VaDestroyContextFn)(VADisplay, VAContextID);
typedef VAStatus (*VaDestroySurfacesFn)(VADisplay, VASurfaceID*, int);

static VaQueryConfigProfilesFn g_real_vaQueryConfigProfiles;
static VaQueryConfigEntrypointsFn g_real_vaQueryConfigEntrypoints;
static VaCreateConfigFn g_real_vaCreateConfig;
static VaGetConfigAttributesFn g_real_vaGetConfigAttributes;
static VaMaxNumProfilesFn g_real_vaMaxNumProfiles;
static VaMaxNumEntrypointsFn g_real_vaMaxNumEntrypoints;
static VaQueryVendorStringFn g_real_vaQueryVendorString;
static VaCreateSurfacesFn g_real_vaCreateSurfaces;
static VaCreateContextFn g_real_vaCreateContext;
static VaCreateBufferFn g_real_vaCreateBuffer;
static VaMapBufferFn g_real_vaMapBuffer;
static VaUnmapBufferFn g_real_vaUnmapBuffer;
static VaBeginPictureFn g_real_vaBeginPicture;
static VaRenderPictureFn g_real_vaRenderPicture;
static VaEndPictureFn g_real_vaEndPicture;
static VaSyncSurfaceFn g_real_vaSyncSurface;
static VaDestroyBufferFn g_real_vaDestroyBuffer;
static VaDestroyContextFn g_real_vaDestroyContext;
static VaDestroySurfacesFn g_real_vaDestroySurfaces;

extern void* dlvsym(void* handle, const char* symbol, const char* version);

VAStatus vaQueryConfigProfiles(VADisplay dpy,
                               VAProfile* profile_list,
                               int* num_profiles);
VAStatus vaQueryConfigEntrypoints(VADisplay dpy,
                                  VAProfile profile,
                                  VAEntrypoint* entrypoint_list,
                                  int* num_entrypoints);
VAStatus vaCreateConfig(VADisplay dpy,
                        VAProfile profile,
                        VAEntrypoint entrypoint,
                        VAConfigAttrib* attrib_list,
                        int num_attribs,
                        VAConfigID* config_id);
VAStatus vaGetConfigAttributes(VADisplay dpy,
                               VAProfile profile,
                               VAEntrypoint entrypoint,
                               VAConfigAttrib* attrib_list,
                               int num_attribs);
int vaMaxNumProfiles(VADisplay dpy);
int vaMaxNumEntrypoints(VADisplay dpy);
const char* vaQueryVendorString(VADisplay dpy);
VAStatus vaCreateSurfaces(VADisplay dpy,
                          unsigned int format,
                          unsigned int width,
                          unsigned int height,
                          VASurfaceID* surfaces,
                          unsigned int num_surfaces,
                          void* attrib_list,
                          unsigned int num_attribs);
VAStatus vaCreateContext(VADisplay dpy,
                         VAConfigID config_id,
                         int picture_width,
                         int picture_height,
                         int flag,
                         VASurfaceID* render_targets,
                         int num_render_targets,
                         VAContextID* context);
VAStatus vaCreateBuffer(VADisplay dpy,
                        VAContextID context,
                        int type,
                        unsigned int size,
                        unsigned int num_elements,
                        void* data,
                        VABufferID* buf_id);
VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void** pbuf);
VAStatus vaUnmapBuffer(VADisplay dpy, VABufferID buf_id);
VAStatus vaBeginPicture(VADisplay dpy, VAContextID context, VASurfaceID render_target);
VAStatus vaRenderPicture(VADisplay dpy,
                         VAContextID context,
                         VABufferID* buffers,
                         int num_buffers);
VAStatus vaEndPicture(VADisplay dpy, VAContextID context);
VAStatus vaSyncSurface(VADisplay dpy, VASurfaceID render_target);
VAStatus vaDestroyBuffer(VADisplay dpy, VABufferID buffer_id);
VAStatus vaDestroyContext(VADisplay dpy, VAContextID context);
VAStatus vaDestroySurfaces(VADisplay dpy, VASurfaceID* surfaces, int num_surfaces);

static bool is_hevc_profile(VAProfile profile) {
  return profile >= (int)VA_PROFILE_HEVC_MAIN &&
         profile <= (int)VA_PROFILE_HEVC_MAIN444_12;
}

static bool is_h264_profile(VAProfile profile) {
  return profile == VA_PROFILE_H264_MAIN || profile == VA_PROFILE_H264_HIGH ||
         profile == VA_PROFILE_H264_CONSTRAINED_BASELINE;
}

static bool is_interesting_decode_profile(VAProfile profile) {
  return is_h264_profile(profile) || is_hevc_profile(profile);
}

static const char* profile_name(VAProfile profile) {
  switch (profile) {
    case VA_PROFILE_H264_MAIN:
      return "VAProfileH264Main";
    case VA_PROFILE_H264_HIGH:
      return "VAProfileH264High";
    case VA_PROFILE_H264_CONSTRAINED_BASELINE:
      return "VAProfileH264ConstrainedBaseline";
    case VA_PROFILE_HEVC_MAIN:
      return "VAProfileHEVCMain";
    case VA_PROFILE_HEVC_MAIN10:
      return "VAProfileHEVCMain10";
    case VA_PROFILE_HEVC_MAIN12:
      return "VAProfileHEVCMain12";
    case VA_PROFILE_HEVC_MAIN422_10:
      return "VAProfileHEVCMain422_10";
    case VA_PROFILE_HEVC_MAIN422_12:
      return "VAProfileHEVCMain422_12";
    case VA_PROFILE_HEVC_MAIN444:
      return "VAProfileHEVCMain444";
    case VA_PROFILE_HEVC_MAIN444_10:
      return "VAProfileHEVCMain444_10";
    case VA_PROFILE_HEVC_MAIN444_12:
      return "VAProfileHEVCMain444_12";
    default:
      return "VAProfileOther";
  }
}

static const char* entrypoint_name(VAEntrypoint entrypoint) {
  switch (entrypoint) {
    case VA_ENTRYPOINT_VLD:
      return "VAEntrypointVLD";
    case 6:
      return "VAEntrypointEncSlice";
    case 8:
      return "VAEntrypointEncSliceLP";
    default:
      return "VAEntrypointOther";
  }
}

static bool env_on(const char* name) {
  const char* value = getenv(name);
  return value && value[0] && strcmp(value, "0") != 0 &&
         strcasecmp(value, "false") != 0 && strcasecmp(value, "no") != 0;
}

static unsigned int align_up_u32(unsigned int value, unsigned int alignment) {
  return alignment ? ((value + alignment - 1) / alignment) * alignment : value;
}

static void open_log(void) {
  const char* path = getenv("CHROME_VAAPI_PROBE_LOG");
  if (path && path[0]) {
    g_log = fopen(path, "a");
  }
  if (!g_log) {
    g_log = stderr;
  }
  setvbuf(g_log, NULL, _IOLBF, 0);
}

static void log_line(const char* fmt, ...) {
  if (!g_log) {
    open_log();
  }

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm;
  localtime_r(&ts.tv_sec, &tm);

  fprintf(g_log, "%04d-%02d-%02dT%02d:%02d:%02d.%03ld pid=%ld ",
          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
          tm.tm_sec, ts.tv_nsec / 1000000, (long)getpid());

  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
}

static DlsymFn real_dlsym_fn(void) {
  static DlsymFn real_fn;
  if (!real_fn) {
    real_fn = (DlsymFn)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
  }
  return real_fn;
}

static void* real_symbol(void* handle, const char* name) {
  DlsymFn real_fn = real_dlsym_fn();
  if (!real_fn) {
    return NULL;
  }
  return real_fn(handle, name);
}

static void* sym(const char* name) {
  void* fn = real_symbol(RTLD_NEXT, name);
  if (!fn) {
    log_line("error dlsym(%s): %s", name, dlerror());
  }
  return fn;
}

void* dlsym(void* handle, const char* symbol) {
  DlsymFn real_fn = real_dlsym_fn();
  if (!real_fn) {
    return NULL;
  }
  if (g_in_dlsym_hook || !symbol) {
    return real_fn(handle, symbol);
  }

  g_in_dlsym_hook = true;
  void* replacement = NULL;
  void* original = NULL;
  if (strcmp(symbol, "vaQueryConfigProfiles") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaQueryConfigProfiles = (VaQueryConfigProfilesFn)original;
    }
    replacement = (void*)vaQueryConfigProfiles;
  } else if (strcmp(symbol, "vaQueryConfigEntrypoints") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaQueryConfigEntrypoints = (VaQueryConfigEntrypointsFn)original;
    }
    replacement = (void*)vaQueryConfigEntrypoints;
  } else if (strcmp(symbol, "vaCreateConfig") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaCreateConfig = (VaCreateConfigFn)original;
    }
    replacement = (void*)vaCreateConfig;
  } else if (strcmp(symbol, "vaGetConfigAttributes") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaGetConfigAttributes = (VaGetConfigAttributesFn)original;
    }
    replacement = (void*)vaGetConfigAttributes;
  } else if (strcmp(symbol, "vaMaxNumProfiles") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaMaxNumProfiles = (VaMaxNumProfilesFn)original;
    }
    replacement = (void*)vaMaxNumProfiles;
  } else if (strcmp(symbol, "vaMaxNumEntrypoints") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaMaxNumEntrypoints = (VaMaxNumEntrypointsFn)original;
    }
    replacement = (void*)vaMaxNumEntrypoints;
  } else if (strcmp(symbol, "vaQueryVendorString") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaQueryVendorString = (VaQueryVendorStringFn)original;
    }
    replacement = (void*)vaQueryVendorString;
  } else if (strcmp(symbol, "vaCreateSurfaces") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaCreateSurfaces = (VaCreateSurfacesFn)original;
    }
    replacement = (void*)vaCreateSurfaces;
  } else if (strcmp(symbol, "vaCreateContext") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaCreateContext = (VaCreateContextFn)original;
    }
    replacement = (void*)vaCreateContext;
  } else if (strcmp(symbol, "vaCreateBuffer") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaCreateBuffer = (VaCreateBufferFn)original;
    }
    replacement = (void*)vaCreateBuffer;
  } else if (strcmp(symbol, "vaMapBuffer") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaMapBuffer = (VaMapBufferFn)original;
    }
    replacement = (void*)vaMapBuffer;
  } else if (strcmp(symbol, "vaUnmapBuffer") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaUnmapBuffer = (VaUnmapBufferFn)original;
    }
    replacement = (void*)vaUnmapBuffer;
  } else if (strcmp(symbol, "vaBeginPicture") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaBeginPicture = (VaBeginPictureFn)original;
    }
    replacement = (void*)vaBeginPicture;
  } else if (strcmp(symbol, "vaRenderPicture") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaRenderPicture = (VaRenderPictureFn)original;
    }
    replacement = (void*)vaRenderPicture;
  } else if (strcmp(symbol, "vaEndPicture") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaEndPicture = (VaEndPictureFn)original;
    }
    replacement = (void*)vaEndPicture;
  } else if (strcmp(symbol, "vaSyncSurface") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaSyncSurface = (VaSyncSurfaceFn)original;
    }
    replacement = (void*)vaSyncSurface;
  } else if (strcmp(symbol, "vaDestroyBuffer") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaDestroyBuffer = (VaDestroyBufferFn)original;
    }
    replacement = (void*)vaDestroyBuffer;
  } else if (strcmp(symbol, "vaDestroyContext") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaDestroyContext = (VaDestroyContextFn)original;
    }
    replacement = (void*)vaDestroyContext;
  } else if (strcmp(symbol, "vaDestroySurfaces") == 0) {
    original = real_fn(handle, symbol);
    if (original) {
      g_real_vaDestroySurfaces = (VaDestroySurfacesFn)original;
    }
    replacement = (void*)vaDestroySurfaces;
  }

  if (replacement) {
    log_line("dlsym-redirect symbol=%s original=%p replacement=%p", symbol,
             original, replacement);
    g_in_dlsym_hook = false;
    return original ? replacement : NULL;
  }

  void* result = real_fn(handle, symbol);
  g_in_dlsym_hook = false;
  return result;
}

static bool contains_int(const int* values, int count, int needle) {
  for (int i = 0; i < count; ++i) {
    if (values[i] == needle) {
      return true;
    }
  }
  return false;
}

static int append_unique(int* values, int count, int max, int value) {
  if (count >= max || contains_int(values, count, value)) {
    return count;
  }
  values[count++] = value;
  return count;
}

int vaMaxNumProfiles(VADisplay dpy) {
  if (!g_real_vaMaxNumProfiles) {
    g_real_vaMaxNumProfiles = (VaMaxNumProfilesFn)sym("vaMaxNumProfiles");
  }
  if (!g_real_vaMaxNumProfiles) {
    return 0;
  }

  int real_max = g_real_vaMaxNumProfiles(dpy);
  int reported_max = real_max;
  if (g_inject_profiles && real_max > 0) {
    reported_max = real_max + 8;
  }
  g_profile_capacity_hint = reported_max;
  log_line("vaMaxNumProfiles real=%d reported=%d", real_max, reported_max);
  return reported_max;
}

int vaMaxNumEntrypoints(VADisplay dpy) {
  if (!g_real_vaMaxNumEntrypoints) {
    g_real_vaMaxNumEntrypoints =
        (VaMaxNumEntrypointsFn)sym("vaMaxNumEntrypoints");
  }
  if (!g_real_vaMaxNumEntrypoints) {
    return 0;
  }

  int real_max = g_real_vaMaxNumEntrypoints(dpy);
  int reported_max = real_max;
  if (g_force_vld && real_max > 0) {
    reported_max = real_max + 1;
  }
  g_entrypoint_capacity_hint = reported_max;
  log_line("vaMaxNumEntrypoints real=%d reported=%d", real_max, reported_max);
  return reported_max;
}

const char* vaQueryVendorString(VADisplay dpy) {
  if (!g_real_vaQueryVendorString) {
    g_real_vaQueryVendorString =
        (VaQueryVendorStringFn)sym("vaQueryVendorString");
  }
  if (!g_real_vaQueryVendorString) {
    return NULL;
  }

  const char* real_vendor = g_real_vaQueryVendorString(dpy);
  const char* spoof_vendor = getenv("CHROME_VAAPI_PROBE_SPOOF_VENDOR");
  if (spoof_vendor && spoof_vendor[0]) {
    log_line("vaQueryVendorString real=\"%s\" spoof=\"%s\"",
             real_vendor ? real_vendor : "", spoof_vendor);
    return spoof_vendor;
  }
  log_line("vaQueryVendorString real=\"%s\"", real_vendor ? real_vendor : "");
  return real_vendor;
}

__attribute__((constructor)) static void init_probe(void) {
  g_inject_profiles = env_on("CHROME_VAAPI_PROBE_INJECT_PROFILES");
  g_force_vld = env_on("CHROME_VAAPI_PROBE_FORCE_VLD");
  g_log_all_profiles = env_on("CHROME_VAAPI_PROBE_LOG_ALL_PROFILES");
  open_log();
  log_line("probe-loaded inject_profiles=%d force_vld=%d log_all_profiles=%d",
           g_inject_profiles, g_force_vld, g_log_all_profiles);
}

VAStatus vaQueryConfigProfiles(VADisplay dpy,
                               VAProfile* profile_list,
                               int* num_profiles) {
  if (!g_real_vaQueryConfigProfiles) {
    g_real_vaQueryConfigProfiles =
        (VaQueryConfigProfilesFn)sym("vaQueryConfigProfiles");
  }
  if (!g_real_vaQueryConfigProfiles) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaQueryConfigProfiles(dpy, profile_list,
                                                 num_profiles);
  int count = num_profiles ? *num_profiles : 0;
  bool has_hevc = false;
  bool has_h264 = false;
  for (int i = 0; profile_list && i < count; ++i) {
    has_hevc = has_hevc || is_hevc_profile(profile_list[i]);
    has_h264 = has_h264 || is_h264_profile(profile_list[i]);
  }

  if (status == VA_STATUS_SUCCESS && g_inject_profiles && profile_list &&
      num_profiles && g_profile_capacity_hint > count) {
    int injected_count = count;
    injected_count = append_unique(profile_list, injected_count,
                                   g_profile_capacity_hint,
                                   VA_PROFILE_HEVC_MAIN);
    injected_count = append_unique(profile_list, injected_count,
                                   g_profile_capacity_hint,
                                   VA_PROFILE_HEVC_MAIN10);
    *num_profiles = injected_count;
    count = injected_count;
    has_hevc = true;
  } else if (status == VA_STATUS_SUCCESS && g_inject_profiles) {
    log_line("vaQueryConfigProfiles injection-skipped capacity_hint=%d count=%d",
             g_profile_capacity_hint, count);
  }

  log_line("vaQueryConfigProfiles status=%d count=%d has_h264=%d has_hevc=%d",
           status, count, has_h264, has_hevc);
  if (profile_list && (has_h264 || has_hevc || g_log_all_profiles)) {
    for (int i = 0; i < count; ++i) {
      if (g_log_all_profiles || is_interesting_decode_profile(profile_list[i])) {
        log_line("  profile[%d]=%d %s", i, profile_list[i],
                 profile_name(profile_list[i]));
      }
    }
  }
  return status;
}

VAStatus vaQueryConfigEntrypoints(VADisplay dpy,
                                  VAProfile profile,
                                  VAEntrypoint* entrypoint_list,
                                  int* num_entrypoints) {
  if (!g_real_vaQueryConfigEntrypoints) {
    g_real_vaQueryConfigEntrypoints =
        (VaQueryConfigEntrypointsFn)sym("vaQueryConfigEntrypoints");
  }
  if (!g_real_vaQueryConfigEntrypoints) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaQueryConfigEntrypoints(dpy, profile,
                                                   entrypoint_list,
                                                   num_entrypoints);
  int count = num_entrypoints ? *num_entrypoints : 0;
  bool has_vld = false;
  for (int i = 0; entrypoint_list && i < count; ++i) {
    has_vld = has_vld || entrypoint_list[i] == VA_ENTRYPOINT_VLD;
  }

  if (status == VA_STATUS_SUCCESS && g_force_vld && is_hevc_profile(profile) &&
      entrypoint_list && num_entrypoints && g_entrypoint_capacity_hint > count) {
    int injected_count = append_unique(entrypoint_list, count,
                                       g_entrypoint_capacity_hint,
                                       VA_ENTRYPOINT_VLD);
    *num_entrypoints = injected_count;
    count = injected_count;
    has_vld = true;
  } else if (status == VA_STATUS_SUCCESS && g_force_vld &&
             is_hevc_profile(profile)) {
    log_line("vaQueryConfigEntrypoints injection-skipped capacity_hint=%d "
             "count=%d",
             g_entrypoint_capacity_hint, count);
  }

  if (is_interesting_decode_profile(profile) || g_log_all_profiles) {
    log_line("vaQueryConfigEntrypoints profile=%d %s status=%d count=%d "
             "has_vld=%d",
             profile, profile_name(profile), status, count, has_vld);
    for (int i = 0; entrypoint_list && i < count; ++i) {
      log_line("  entrypoint[%d]=%d %s", i, entrypoint_list[i],
               entrypoint_name(entrypoint_list[i]));
    }
  }
  return status;
}

VAStatus vaCreateConfig(VADisplay dpy,
                        VAProfile profile,
                        VAEntrypoint entrypoint,
                        VAConfigAttrib* attrib_list,
                        int num_attribs,
                        VAConfigID* config_id) {
  if (!g_real_vaCreateConfig) {
    g_real_vaCreateConfig = (VaCreateConfigFn)sym("vaCreateConfig");
  }
  if (!g_real_vaCreateConfig) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status =
      g_real_vaCreateConfig(dpy, profile, entrypoint, attrib_list, num_attribs,
                            config_id);
  if (is_interesting_decode_profile(profile) || g_log_all_profiles) {
    log_line("vaCreateConfig profile=%d %s entrypoint=%d %s attrs=%d "
             "status=%d config=%u",
             profile, profile_name(profile), entrypoint,
             entrypoint_name(entrypoint), num_attribs, status,
             config_id ? *config_id : 0);
  }
  return status;
}

VAStatus vaGetConfigAttributes(VADisplay dpy,
                               VAProfile profile,
                               VAEntrypoint entrypoint,
                               VAConfigAttrib* attrib_list,
                               int num_attribs) {
  if (!g_real_vaGetConfigAttributes) {
    g_real_vaGetConfigAttributes =
        (VaGetConfigAttributesFn)sym("vaGetConfigAttributes");
  }
  if (!g_real_vaGetConfigAttributes) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status =
      g_real_vaGetConfigAttributes(dpy, profile, entrypoint, attrib_list,
                                   num_attribs);
  if (is_interesting_decode_profile(profile) || g_log_all_profiles) {
    log_line("vaGetConfigAttributes profile=%d %s entrypoint=%d %s attrs=%d "
             "status=%d",
             profile, profile_name(profile), entrypoint,
             entrypoint_name(entrypoint), num_attribs, status);
    for (int i = 0; attrib_list && i < num_attribs; ++i) {
      log_line("  attrib[%d].type=%d value=0x%x", i, attrib_list[i].type,
               attrib_list[i].value);
    }
  }
  return status;
}

VAStatus vaCreateSurfaces(VADisplay dpy,
                          unsigned int format,
                          unsigned int width,
                          unsigned int height,
                          VASurfaceID* surfaces,
                          unsigned int num_surfaces,
                          void* attrib_list,
                          unsigned int num_attribs) {
  if (!g_real_vaCreateSurfaces) {
    g_real_vaCreateSurfaces = (VaCreateSurfacesFn)sym("vaCreateSurfaces");
  }
  if (!g_real_vaCreateSurfaces) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  const unsigned int original_width = width;
  const unsigned int original_height = height;
  if (env_on("CHROME_VAAPI_PROBE_ALIGN_SURFACE_16")) {
    width = align_up_u32(width, 16);
    height = align_up_u32(height, 16);
  }

  VAStatus status = g_real_vaCreateSurfaces(
      dpy, format, width, height, surfaces, num_surfaces, attrib_list,
      num_attribs);
  log_line("vaCreateSurfaces format=0x%x size=%ux%u requested=%ux%u count=%u "
           "attrs=%u status=%d first_surface=%u",
           format, width, height, original_width, original_height,
           num_surfaces, num_attribs, status,
           surfaces && num_surfaces ? surfaces[0] : 0);
  return status;
}

VAStatus vaCreateContext(VADisplay dpy,
                         VAConfigID config_id,
                         int picture_width,
                         int picture_height,
                         int flag,
                         VASurfaceID* render_targets,
                         int num_render_targets,
                         VAContextID* context) {
  if (!g_real_vaCreateContext) {
    g_real_vaCreateContext = (VaCreateContextFn)sym("vaCreateContext");
  }
  if (!g_real_vaCreateContext) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaCreateContext(
      dpy, config_id, picture_width, picture_height, flag, render_targets,
      num_render_targets, context);
  log_line("vaCreateContext config=%u size=%dx%d flag=0x%x targets=%d "
           "status=%d context=%u first_target=%u",
           config_id, picture_width, picture_height, flag, num_render_targets,
           status, context ? *context : 0,
           render_targets && num_render_targets > 0 ? render_targets[0] : 0);
  return status;
}

VAStatus vaCreateBuffer(VADisplay dpy,
                        VAContextID context,
                        int type,
                        unsigned int size,
                        unsigned int num_elements,
                        void* data,
                        VABufferID* buf_id) {
  if (!g_real_vaCreateBuffer) {
    g_real_vaCreateBuffer = (VaCreateBufferFn)sym("vaCreateBuffer");
  }
  if (!g_real_vaCreateBuffer) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaCreateBuffer(dpy, context, type, size,
                                          num_elements, data, buf_id);
  log_line("vaCreateBuffer context=%u type=%d size=%u elements=%u data=%p "
           "status=%d buffer=%u",
           context, type, size, num_elements, data, status,
           buf_id ? *buf_id : 0);
  return status;
}

VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void** pbuf) {
  if (!g_real_vaMapBuffer) {
    g_real_vaMapBuffer = (VaMapBufferFn)sym("vaMapBuffer");
  }
  if (!g_real_vaMapBuffer) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaMapBuffer(dpy, buf_id, pbuf);
  log_line("vaMapBuffer buffer=%u status=%d ptr=%p", buf_id, status,
           pbuf ? *pbuf : NULL);
  return status;
}

VAStatus vaUnmapBuffer(VADisplay dpy, VABufferID buf_id) {
  if (!g_real_vaUnmapBuffer) {
    g_real_vaUnmapBuffer = (VaUnmapBufferFn)sym("vaUnmapBuffer");
  }
  if (!g_real_vaUnmapBuffer) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaUnmapBuffer(dpy, buf_id);
  log_line("vaUnmapBuffer buffer=%u status=%d", buf_id, status);
  return status;
}

VAStatus vaBeginPicture(VADisplay dpy,
                        VAContextID context,
                        VASurfaceID render_target) {
  if (!g_real_vaBeginPicture) {
    g_real_vaBeginPicture = (VaBeginPictureFn)sym("vaBeginPicture");
  }
  if (!g_real_vaBeginPicture) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaBeginPicture(dpy, context, render_target);
  log_line("vaBeginPicture context=%u target=%u status=%d", context,
           render_target, status);
  return status;
}

VAStatus vaRenderPicture(VADisplay dpy,
                         VAContextID context,
                         VABufferID* buffers,
                         int num_buffers) {
  if (!g_real_vaRenderPicture) {
    g_real_vaRenderPicture = (VaRenderPictureFn)sym("vaRenderPicture");
  }
  if (!g_real_vaRenderPicture) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaRenderPicture(dpy, context, buffers, num_buffers);
  log_line("vaRenderPicture context=%u buffers=%d first_buffer=%u status=%d",
           context, num_buffers, buffers && num_buffers > 0 ? buffers[0] : 0,
           status);
  return status;
}

VAStatus vaEndPicture(VADisplay dpy, VAContextID context) {
  if (!g_real_vaEndPicture) {
    g_real_vaEndPicture = (VaEndPictureFn)sym("vaEndPicture");
  }
  if (!g_real_vaEndPicture) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaEndPicture(dpy, context);
  log_line("vaEndPicture context=%u status=%d", context, status);
  return status;
}

VAStatus vaSyncSurface(VADisplay dpy, VASurfaceID render_target) {
  if (!g_real_vaSyncSurface) {
    g_real_vaSyncSurface = (VaSyncSurfaceFn)sym("vaSyncSurface");
  }
  if (!g_real_vaSyncSurface) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaSyncSurface(dpy, render_target);
  log_line("vaSyncSurface target=%u status=%d", render_target, status);
  return status;
}

VAStatus vaDestroyBuffer(VADisplay dpy, VABufferID buffer_id) {
  if (!g_real_vaDestroyBuffer) {
    g_real_vaDestroyBuffer = (VaDestroyBufferFn)sym("vaDestroyBuffer");
  }
  if (!g_real_vaDestroyBuffer) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaDestroyBuffer(dpy, buffer_id);
  log_line("vaDestroyBuffer buffer=%u status=%d", buffer_id, status);
  return status;
}

VAStatus vaDestroyContext(VADisplay dpy, VAContextID context) {
  if (!g_real_vaDestroyContext) {
    g_real_vaDestroyContext = (VaDestroyContextFn)sym("vaDestroyContext");
  }
  if (!g_real_vaDestroyContext) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaDestroyContext(dpy, context);
  log_line("vaDestroyContext context=%u status=%d", context, status);
  return status;
}

VAStatus vaDestroySurfaces(VADisplay dpy,
                           VASurfaceID* surfaces,
                           int num_surfaces) {
  if (!g_real_vaDestroySurfaces) {
    g_real_vaDestroySurfaces = (VaDestroySurfacesFn)sym("vaDestroySurfaces");
  }
  if (!g_real_vaDestroySurfaces) {
    return VA_STATUS_ERROR_UNKNOWN;
  }

  VAStatus status = g_real_vaDestroySurfaces(dpy, surfaces, num_surfaces);
  log_line("vaDestroySurfaces count=%d first_surface=%u status=%d",
           num_surfaces, surfaces && num_surfaces > 0 ? surfaces[0] : 0,
           status);
  return status;
}
