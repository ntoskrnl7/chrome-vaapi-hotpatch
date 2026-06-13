#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
typedef int VASurfaceAttribType;
typedef int VABufferType;

enum { kVAEncCodedBufferType = 21 };

typedef struct {
  VAConfigAttribType type;
  unsigned int value;
} VAConfigAttrib;
typedef struct {
  VASurfaceAttribType type;
  unsigned int flags;
  unsigned int value_type;
  unsigned long long value[2];
} VASurfaceAttrib;
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
typedef VAStatus (*VaCreateContextFn)(VADisplay,
                                      VAConfigID,
                                      int,
                                      int,
                                      int,
                                      VASurfaceID*,
                                      int,
                                      VAContextID*);
typedef VAStatus (*VaCreateSurfacesFn)(VADisplay,
                                       unsigned int,
                                       unsigned int,
                                       unsigned int,
                                       VASurfaceID*,
                                       unsigned int,
                                       VASurfaceAttrib*,
                                       unsigned int);
typedef VAStatus (*VaCreateBufferFn)(VADisplay,
                                     VAContextID,
                                     VABufferType,
                                     unsigned int,
                                     unsigned int,
                                     void*,
                                     VABufferID*);
typedef VAStatus (*VaBeginPictureFn)(VADisplay, VAContextID, VASurfaceID);
typedef VAStatus (*VaRenderPictureFn)(VADisplay, VAContextID, VABufferID*, int);
typedef VAStatus (*VaEndPictureFn)(VADisplay, VAContextID);
typedef VAStatus (*VaMapBufferFn)(VADisplay, VABufferID, void**);
typedef VAStatus (*VaUnmapBufferFn)(VADisplay, VABufferID);
typedef const char* (*VaQueryVendorStringFn)(VADisplay);

enum {
  kVAProfileHEVCMain = 17,
  kVAProfileHEVCMain10 = 18,
  kVAEntrypointEncSlice = 6,
  kVAConfigAttribEncPackedHeaders = 10,
  kVAPackedHeaderSequencePictureSlice = 0x7,
};

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
VAStatus vaCreateContext(VADisplay dpy,
                         VAConfigID config_id,
                         int picture_width,
                         int picture_height,
                         int flag,
                         VASurfaceID* render_targets,
                         int num_render_targets,
                         VAContextID* context);
VAStatus vaCreateSurfaces(VADisplay dpy,
                          unsigned int format,
                          unsigned int width,
                          unsigned int height,
                          VASurfaceID* surfaces,
                          unsigned int num_surfaces,
                          VASurfaceAttrib* attrib_list,
                          unsigned int num_attribs);
VAStatus vaCreateBuffer(VADisplay dpy,
                        VAContextID context,
                        VABufferType type,
                        unsigned int size,
                        unsigned int num_elements,
                        void* data,
                        VABufferID* buf_id);
VAStatus vaBeginPicture(VADisplay dpy,
                        VAContextID context,
                        VASurfaceID render_target);
VAStatus vaRenderPicture(VADisplay dpy,
                         VAContextID context,
                         VABufferID* buffers,
                         int num_buffers);
VAStatus vaEndPicture(VADisplay dpy, VAContextID context);
VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void** pbuf);
VAStatus vaUnmapBuffer(VADisplay dpy, VABufferID buf_id);
const char* vaQueryVendorString(VADisplay dpy);

static FILE* g_log;
static __thread int g_in_dlsym_hook;
static VaQueryConfigProfilesFn g_real_vaQueryConfigProfiles;
static VaQueryConfigEntrypointsFn g_real_vaQueryConfigEntrypoints;
static VaCreateConfigFn g_real_vaCreateConfig;
static VaCreateContextFn g_real_vaCreateContext;
static VaCreateSurfacesFn g_real_vaCreateSurfaces;
static VaCreateBufferFn g_real_vaCreateBuffer;
static VaBeginPictureFn g_real_vaBeginPicture;
static VaRenderPictureFn g_real_vaRenderPicture;
static VaEndPictureFn g_real_vaEndPicture;
static VaMapBufferFn g_real_vaMapBuffer;
static VaUnmapBufferFn g_real_vaUnmapBuffer;
static VaQueryVendorStringFn g_real_vaQueryVendorString;
static VABufferID g_last_coded_buffer_id;

typedef void* (*DlsymFn)(void*, const char*);

static int env_on(const char* name) {
  const char* value = getenv(name);
  return value && value[0] && strcmp(value, "0") != 0 &&
         strcasecmp(value, "false") != 0 && strcasecmp(value, "no") != 0;
}

static const char* process_type(void) {
  FILE* f = fopen("/proc/self/cmdline", "rb");
  if (!f) {
    return "unknown";
  }

  static char cmdline[8192];
  size_t n = fread(cmdline, 1, sizeof(cmdline) - 1, f);
  fclose(f);
  cmdline[n] = 0;

  for (size_t i = 0; i + 1 < n; ++i) {
    if (cmdline[i] == 0) {
      cmdline[i] = ' ';
    }
  }

  const char* marker = strstr(cmdline, "--type=");
  if (!marker) {
    return "browser";
  }
  marker += strlen("--type=");

  static char type[64];
  size_t i = 0;
  while (marker[i] && marker[i] != ' ' && i + 1 < sizeof(type)) {
    type[i] = marker[i];
    ++i;
  }
  type[i] = 0;
  return type;
}

static void scout_log(const char* fmt, ...) {
  if (!g_log) {
    const char* path = getenv("CHROME_HEVC_DELEGATE_SCOUT_LOG");
    if (!path || !*path) {
      path = "/tmp/chrome-hevc-delegate-scout.log";
    }
    g_log = fopen(path, "a");
    if (!g_log) {
      return;
    }
    setvbuf(g_log, NULL, _IOLBF, 0);
  }

  fprintf(g_log, "[pid=%d type=%s] ", getpid(), process_type());
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
}

__attribute__((constructor)) static void init(void) {
  scout_log("loaded");
}

extern void* dlvsym(void* handle, const char* symbol, const char* version);

static DlsymFn real_dlsym_fn(void) {
  static DlsymFn real_fn;
  if (!real_fn) {
    real_fn = (DlsymFn)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
  }
  return real_fn;
}

static void* real_symbol(void* handle, const char* name) {
  DlsymFn fn = real_dlsym_fn();
  if (!fn) {
    return NULL;
  }
  void* sym = fn(handle, name);
  if (!sym) {
    scout_log("missing real symbol: %s", name);
  }
  return sym;
}

void* dlsym(void* handle, const char* symbol) {
  DlsymFn fn = real_dlsym_fn();
  if (!fn) {
    return NULL;
  }
  if (g_in_dlsym_hook) {
    return fn(handle, symbol);
  }

  g_in_dlsym_hook = 1;
  void* original = fn(handle, symbol);
  void* replacement = NULL;

  if (strcmp(symbol, "vaQueryConfigProfiles") == 0) {
    g_real_vaQueryConfigProfiles = (VaQueryConfigProfilesFn)original;
    replacement = (void*)vaQueryConfigProfiles;
  } else if (strcmp(symbol, "vaQueryConfigEntrypoints") == 0) {
    g_real_vaQueryConfigEntrypoints = (VaQueryConfigEntrypointsFn)original;
    replacement = (void*)vaQueryConfigEntrypoints;
  } else if (strcmp(symbol, "vaCreateConfig") == 0) {
    g_real_vaCreateConfig = (VaCreateConfigFn)original;
    replacement = (void*)vaCreateConfig;
  } else if (strcmp(symbol, "vaCreateContext") == 0) {
    g_real_vaCreateContext = (VaCreateContextFn)original;
    replacement = (void*)vaCreateContext;
  } else if (strcmp(symbol, "vaCreateSurfaces") == 0) {
    g_real_vaCreateSurfaces = (VaCreateSurfacesFn)original;
    replacement = (void*)vaCreateSurfaces;
  } else if (strcmp(symbol, "vaCreateBuffer") == 0) {
    g_real_vaCreateBuffer = (VaCreateBufferFn)original;
    replacement = (void*)vaCreateBuffer;
  } else if (strcmp(symbol, "vaBeginPicture") == 0) {
    g_real_vaBeginPicture = (VaBeginPictureFn)original;
    replacement = (void*)vaBeginPicture;
  } else if (strcmp(symbol, "vaRenderPicture") == 0) {
    g_real_vaRenderPicture = (VaRenderPictureFn)original;
    replacement = (void*)vaRenderPicture;
  } else if (strcmp(symbol, "vaEndPicture") == 0) {
    g_real_vaEndPicture = (VaEndPictureFn)original;
    replacement = (void*)vaEndPicture;
  } else if (strcmp(symbol, "vaMapBuffer") == 0) {
    g_real_vaMapBuffer = (VaMapBufferFn)original;
    replacement = (void*)vaMapBuffer;
  } else if (strcmp(symbol, "vaUnmapBuffer") == 0) {
    g_real_vaUnmapBuffer = (VaUnmapBufferFn)original;
    replacement = (void*)vaUnmapBuffer;
  } else if (strcmp(symbol, "vaQueryVendorString") == 0) {
    g_real_vaQueryVendorString = (VaQueryVendorStringFn)original;
    replacement = (void*)vaQueryVendorString;
  }

  if (replacement) {
    scout_log("dlsym redirect %s original=%p replacement=%p", symbol, original,
              replacement);
    g_in_dlsym_hook = 0;
    return replacement;
  }

  g_in_dlsym_hook = 0;
  return original;
}

VAStatus vaQueryConfigProfiles(VADisplay dpy,
                               VAProfile* profile_list,
                               int* num_profiles) {
  if (!g_real_vaQueryConfigProfiles) {
    g_real_vaQueryConfigProfiles =
        (VaQueryConfigProfilesFn)real_symbol(RTLD_NEXT,
                                             "vaQueryConfigProfiles");
  }

  VAStatus ret =
      g_real_vaQueryConfigProfiles(dpy, profile_list, num_profiles);
  int saw_hevc_main = 0;
  int saw_hevc_main10 = 0;
  if (profile_list && num_profiles && *num_profiles > 0) {
    for (int i = 0; i < *num_profiles; ++i) {
      saw_hevc_main |= profile_list[i] == kVAProfileHEVCMain;
      saw_hevc_main10 |= profile_list[i] == kVAProfileHEVCMain10;
    }
  }
  scout_log("vaQueryConfigProfiles ret=%d count=%d hevc_main=%d hevc_main10=%d",
            ret, num_profiles ? *num_profiles : -1, saw_hevc_main,
            saw_hevc_main10);
  return ret;
}

const char* vaQueryVendorString(VADisplay dpy) {
  if (!g_real_vaQueryVendorString) {
    g_real_vaQueryVendorString =
        (VaQueryVendorStringFn)real_symbol(RTLD_NEXT, "vaQueryVendorString");
  }

  const char* original = g_real_vaQueryVendorString
                             ? g_real_vaQueryVendorString(dpy)
                             : NULL;
  const char* override = getenv("CHROME_HEVC_SCOUT_VENDOR_STRING");
  if (override && *override) {
    scout_log("vaQueryVendorString original=\"%s\" override=\"%s\"",
              original ? original : "", override);
    return override;
  }
  scout_log("vaQueryVendorString ret=\"%s\"", original ? original : "");
  return original;
}

VAStatus vaQueryConfigEntrypoints(VADisplay dpy,
                                  VAProfile profile,
                                  VAEntrypoint* entrypoint_list,
                                  int* num_entrypoints) {
  if (!g_real_vaQueryConfigEntrypoints) {
    g_real_vaQueryConfigEntrypoints =
        (VaQueryConfigEntrypointsFn)real_symbol(RTLD_NEXT,
                                                "vaQueryConfigEntrypoints");
  }

  VAStatus ret = g_real_vaQueryConfigEntrypoints(
      dpy, profile, entrypoint_list, num_entrypoints);
  if (profile == kVAProfileHEVCMain || profile == kVAProfileHEVCMain10) {
    int saw_enc_slice = 0;
    if (entrypoint_list && num_entrypoints && *num_entrypoints > 0) {
      for (int i = 0; i < *num_entrypoints; ++i) {
        saw_enc_slice |= entrypoint_list[i] == kVAEntrypointEncSlice;
      }
    }
    scout_log("vaQueryConfigEntrypoints profile=%d ret=%d count=%d enc_slice=%d",
              profile, ret, num_entrypoints ? *num_entrypoints : -1,
              saw_enc_slice);
  }
  return ret;
}

VAStatus vaCreateConfig(VADisplay dpy,
                        VAProfile profile,
                        VAEntrypoint entrypoint,
                        VAConfigAttrib* attrib_list,
                        int num_attribs,
                        VAConfigID* config_id) {
  if (!g_real_vaCreateConfig) {
    g_real_vaCreateConfig =
        (VaCreateConfigFn)real_symbol(RTLD_NEXT, "vaCreateConfig");
  }

  VAConfigAttrib patched_attribs[8];
  if ((profile == kVAProfileHEVCMain || profile == kVAProfileHEVCMain10) &&
      entrypoint == kVAEntrypointEncSlice &&
      env_on("CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS") && attrib_list &&
      num_attribs > 0 && num_attribs < (int)(sizeof(patched_attribs) /
                                             sizeof(patched_attribs[0]))) {
    int has_packed_headers = 0;
    for (int i = 0; i < num_attribs; ++i) {
      patched_attribs[i] = attrib_list[i];
      has_packed_headers |=
          attrib_list[i].type == kVAConfigAttribEncPackedHeaders;
    }
    if (!has_packed_headers) {
      patched_attribs[num_attribs].type = kVAConfigAttribEncPackedHeaders;
      patched_attribs[num_attribs].value =
          kVAPackedHeaderSequencePictureSlice;
      attrib_list = patched_attribs;
      ++num_attribs;
      scout_log("appended HEVC packed-header attrib before vaCreateConfig");
    }
  }

  if (profile == kVAProfileHEVCMain || profile == kVAProfileHEVCMain10 ||
      entrypoint == kVAEntrypointEncSlice) {
    scout_log("vaCreateConfig call profile=%d entrypoint=%d attribs=%d",
              profile, entrypoint, num_attribs);
    for (int i = 0; attrib_list && i < num_attribs; ++i) {
      scout_log("  attr[%d].type=%d value=0x%x", i, attrib_list[i].type,
                attrib_list[i].value);
    }
  }
  VAStatus ret = g_real_vaCreateConfig(dpy, profile, entrypoint, attrib_list,
                                       num_attribs, config_id);
  if (profile == kVAProfileHEVCMain || profile == kVAProfileHEVCMain10 ||
      entrypoint == kVAEntrypointEncSlice) {
    scout_log("vaCreateConfig ret=%d profile=%d entrypoint=%d config=%u", ret,
              profile, entrypoint, config_id ? *config_id : 0);
  }
  return ret;
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
    g_real_vaCreateContext =
        (VaCreateContextFn)real_symbol(RTLD_NEXT, "vaCreateContext");
  }

  int patched_width = picture_width;
  int patched_height = picture_height;
  const char* force_width = getenv("CHROME_HEVC_SCOUT_FORCE_CONTEXT_WIDTH");
  const char* force_height = getenv("CHROME_HEVC_SCOUT_FORCE_CONTEXT_HEIGHT");
  if (force_width && *force_width) {
    patched_width = atoi(force_width);
  }
  if (force_height && *force_height) {
    patched_height = atoi(force_height);
  }

  if (patched_width != picture_width || patched_height != picture_height) {
    scout_log("vaCreateContext override config=%u %dx%d -> %dx%d flag=0x%x "
              "targets=%d",
              config_id, picture_width, picture_height, patched_width,
              patched_height, flag, num_render_targets);
  } else {
    scout_log("vaCreateContext call config=%u size=%dx%d flag=0x%x targets=%d",
              config_id, picture_width, picture_height, flag,
              num_render_targets);
  }

  VAStatus ret =
      g_real_vaCreateContext(dpy, config_id, patched_width, patched_height,
                             flag, render_targets, num_render_targets, context);
  scout_log("vaCreateContext ret=%d config=%u context=%u", ret, config_id,
            context ? *context : 0);
  return ret;
}

VAStatus vaCreateSurfaces(VADisplay dpy,
                          unsigned int format,
                          unsigned int width,
                          unsigned int height,
                          VASurfaceID* surfaces,
                          unsigned int num_surfaces,
                          VASurfaceAttrib* attrib_list,
                          unsigned int num_attribs) {
  if (!g_real_vaCreateSurfaces) {
    g_real_vaCreateSurfaces =
        (VaCreateSurfacesFn)real_symbol(RTLD_NEXT, "vaCreateSurfaces");
  }

  unsigned int patched_width = width;
  unsigned int patched_height = height;
  const char* force_width = getenv("CHROME_HEVC_SCOUT_FORCE_SURFACE_WIDTH");
  const char* force_height = getenv("CHROME_HEVC_SCOUT_FORCE_SURFACE_HEIGHT");
  if (!force_width || !*force_width) {
    force_width = getenv("CHROME_HEVC_SCOUT_FORCE_CONTEXT_WIDTH");
  }
  if (!force_height || !*force_height) {
    force_height = getenv("CHROME_HEVC_SCOUT_FORCE_CONTEXT_HEIGHT");
  }
  if (force_width && *force_width) {
    patched_width = (unsigned int)strtoul(force_width, NULL, 0);
  }
  if (force_height && *force_height) {
    patched_height = (unsigned int)strtoul(force_height, NULL, 0);
  }

  if (patched_width != width || patched_height != height) {
    scout_log("vaCreateSurfaces override format=0x%x %ux%u -> %ux%u "
              "count=%u attribs=%u",
              format, width, height, patched_width, patched_height,
              num_surfaces, num_attribs);
  } else {
    scout_log("vaCreateSurfaces call format=0x%x size=%ux%u count=%u "
              "attribs=%u",
              format, width, height, num_surfaces, num_attribs);
  }
  for (unsigned int i = 0; attrib_list && i < num_attribs; ++i) {
    scout_log("  surface_attr[%u].type=%d flags=0x%x value_type=%u "
              "value0=0x%llx value1=0x%llx",
              i, attrib_list[i].type, attrib_list[i].flags,
              attrib_list[i].value_type,
              (unsigned long long)attrib_list[i].value[0],
              (unsigned long long)attrib_list[i].value[1]);
  }

  VAStatus ret =
      g_real_vaCreateSurfaces(dpy, format, patched_width, patched_height,
                              surfaces, num_surfaces, attrib_list, num_attribs);
  scout_log("vaCreateSurfaces ret=%d first_surface=%u", ret,
            surfaces && num_surfaces ? surfaces[0] : 0);
  return ret;
}

VAStatus vaCreateBuffer(VADisplay dpy,
                        VAContextID context,
                        VABufferType type,
                        unsigned int size,
                        unsigned int num_elements,
                        void* data,
                        VABufferID* buf_id) {
  if (!g_real_vaCreateBuffer) {
    g_real_vaCreateBuffer =
        (VaCreateBufferFn)real_symbol(RTLD_NEXT, "vaCreateBuffer");
  }

  unsigned int patched_size = size;
  const char* force_coded_size =
      getenv("CHROME_HEVC_SCOUT_FORCE_CODED_BUFFER_SIZE");
  if (type == kVAEncCodedBufferType && force_coded_size && *force_coded_size) {
    patched_size = (unsigned int)strtoul(force_coded_size, NULL, 0);
  }

  if (patched_size != size) {
    scout_log("vaCreateBuffer override context=%u type=%d size=%u -> %u "
              "elements=%u data=%p",
              context, type, size, patched_size, num_elements, data);
  } else {
    scout_log("vaCreateBuffer call context=%u type=%d size=%u elements=%u "
              "data=%p",
              context, type, size, num_elements, data);
  }

  VAStatus ret = g_real_vaCreateBuffer(dpy, context, type, patched_size,
                                       num_elements, data, buf_id);
  scout_log("vaCreateBuffer ret=%d context=%u type=%d buffer=%u", ret, context,
            type, buf_id ? *buf_id : 0);
  if (ret == 0 && type == kVAEncCodedBufferType && buf_id) {
    g_last_coded_buffer_id = *buf_id;
  }
  return ret;
}

VAStatus vaBeginPicture(VADisplay dpy,
                        VAContextID context,
                        VASurfaceID render_target) {
  if (!g_real_vaBeginPicture) {
    g_real_vaBeginPicture =
        (VaBeginPictureFn)real_symbol(RTLD_NEXT, "vaBeginPicture");
  }
  scout_log("vaBeginPicture context=%u surface=%u", context, render_target);
  VAStatus ret = g_real_vaBeginPicture(dpy, context, render_target);
  scout_log("vaBeginPicture ret=%d", ret);
  return ret;
}

VAStatus vaRenderPicture(VADisplay dpy,
                         VAContextID context,
                         VABufferID* buffers,
                         int num_buffers) {
  if (!g_real_vaRenderPicture) {
    g_real_vaRenderPicture =
        (VaRenderPictureFn)real_symbol(RTLD_NEXT, "vaRenderPicture");
  }
  scout_log("vaRenderPicture context=%u buffers=%d first=%u", context,
            num_buffers, buffers && num_buffers > 0 ? buffers[0] : 0);
  VAStatus ret = g_real_vaRenderPicture(dpy, context, buffers, num_buffers);
  scout_log("vaRenderPicture ret=%d", ret);
  return ret;
}

VAStatus vaEndPicture(VADisplay dpy, VAContextID context) {
  if (!g_real_vaEndPicture) {
    g_real_vaEndPicture = (VaEndPictureFn)real_symbol(RTLD_NEXT, "vaEndPicture");
  }
  scout_log("vaEndPicture context=%u", context);
  VAStatus ret = g_real_vaEndPicture(dpy, context);
  scout_log("vaEndPicture ret=%d", ret);
  return ret;
}

VAStatus vaMapBuffer(VADisplay dpy, VABufferID buf_id, void** pbuf) {
  if (!g_real_vaMapBuffer) {
    g_real_vaMapBuffer = (VaMapBufferFn)real_symbol(RTLD_NEXT, "vaMapBuffer");
  }
  scout_log("vaMapBuffer buffer=%u", buf_id);
  VAStatus ret = g_real_vaMapBuffer(dpy, buf_id, pbuf);
  scout_log("vaMapBuffer ret=%d ptr=%p", ret, pbuf ? *pbuf : NULL);
  if (ret == 0 && pbuf && *pbuf && buf_id == g_last_coded_buffer_id) {
    const unsigned char* bytes = (const unsigned char*)*pbuf;
    char hex[64 * 3 + 1];
    size_t pos = 0;
    for (unsigned int i = 0; i < 64 && pos + 3 < sizeof(hex); ++i) {
      pos += (size_t)snprintf(hex + pos, sizeof(hex) - pos, "%02x%s",
                              bytes[i], i + 1 == 64 ? "" : " ");
    }
    hex[pos] = 0;
    scout_log("coded segment raw64=%s", hex);

    unsigned int segment_size = 0;
    unsigned int segment_status = 0;
    const unsigned char* segment_buf = NULL;
    void* segment_next = NULL;
    memcpy(&segment_size, bytes, sizeof(segment_size));
    memcpy(&segment_status, bytes + 8, sizeof(segment_status));
    memcpy(&segment_buf, bytes + 16, sizeof(segment_buf));
    memcpy(&segment_next, bytes + 24, sizeof(segment_next));
    if (segment_buf && segment_size > 0 && segment_size < 1024 * 1024) {
      unsigned int data_dump = segment_size < 96 ? segment_size : 96;
      char data_hex[96 * 3 + 1];
      pos = 0;
      for (unsigned int i = 0;
           i < data_dump && pos + 3 < sizeof(data_hex); ++i) {
        pos += (size_t)snprintf(data_hex + pos, sizeof(data_hex) - pos,
                                "%02x%s", segment_buf[i],
                                i + 1 == data_dump ? "" : " ");
      }
      data_hex[pos] = 0;
      scout_log("coded segment parsed size=%u status=0x%x buf=%p next=%p "
                "data=%s",
                segment_size, segment_status, segment_buf, segment_next,
                data_hex);
    }
  }
  return ret;
}

VAStatus vaUnmapBuffer(VADisplay dpy, VABufferID buf_id) {
  if (!g_real_vaUnmapBuffer) {
    g_real_vaUnmapBuffer =
        (VaUnmapBufferFn)real_symbol(RTLD_NEXT, "vaUnmapBuffer");
  }
  scout_log("vaUnmapBuffer buffer=%u", buf_id);
  VAStatus ret = g_real_vaUnmapBuffer(dpy, buf_id);
  scout_log("vaUnmapBuffer ret=%d", ret);
  return ret;
}
