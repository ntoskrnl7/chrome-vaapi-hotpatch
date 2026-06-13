#define _GNU_SOURCE

#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <link.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

// Chrome 146.0.7680.177, build-id 5570e100d5af8a240754617d8bce01d4a868040d.
// This is the likely VaapiVideoEncodeAccelerator::Initialize entry point found
// by tools/scan_chrome_hevc_hook_candidates.py.  Objdump reports VMA
// 0x8c60d10, while the executable mapping uses the file-backed offset
// 0x8c5fd10 because Chrome's section VMA is file offset + 0x1000.
static const uintptr_t kChrome146VeaInitializeOffset = 0x8c5fd10;
static const uintptr_t kChrome146VeaInitializeTaskOffset = 0x8c607a0;
static const uintptr_t kChrome146VeaGetSupportedProfilesOffset = 0x8c5f660;
static const uintptr_t kChrome146VeaCtorOffset = 0x8c5f6d0;
static const uintptr_t kChrome146VaapiCreateForVideoCodecOffset = 0x8c73150;
static const uintptr_t kChrome146VaapiCreateOffset = 0x8c72b90;
static const uintptr_t kChrome146VaapiCreateBypassProfileTableMissOffset =
    0x8c72be2;
static const uintptr_t kChrome146VeaAcceptedCodecMaskOffset = 0x8c5ff19;
static const uintptr_t kChrome146VeaBypassVbrRestrictionOffset = 0x8c5ff28;
static const uintptr_t kChrome146VeaDefaultEncodeModeOffset = 0x8c60853;
static const uintptr_t kChrome146VeaAcceptVp8Vp9RangeCmpOffset = 0x8c6085c;
static const uintptr_t kChrome146VeaForceModeAcceptOffset = 0x8c60866;
static const uintptr_t kChrome146VeaDelegateSwitchHevcEntryOffset = 0x2366d08;
static const uintptr_t kChrome146H264DelegateCtorOffset = 0x8c54b80;
static const uintptr_t kChrome146DelegateCfiCheckAOffset = 0x8c6099e;
static const uintptr_t kChrome146DelegateCfiCheckBOffset = 0x8c60a4e;
static const uintptr_t kChrome146DelegateCfiCheckCOffset = 0x8c60b94;
static const uintptr_t kChrome146DelegateCfiCheckDOffset = 0x8c60d56;
static const uintptr_t kChrome146DelegateCfiCheckInitOffset = 0x8c60e5e;
static const uintptr_t kChrome146DelegateInitializeCfiSlowpathCallOffset =
    0x8c5ff80;
static const uintptr_t kChrome146DelegateInitializeCfiRuntimeCallOffset =
    0x8c60f8b;
static const uintptr_t kChrome146DelegateCleanupShortCfiBranchOffset =
    0x8c65c13;
static const uintptr_t kChrome146DelegateCfiCheckGetFramesOffset = 0x8c60fd8;
static const uintptr_t kChrome146DelegateCfiCheckGetBitstreamOffset = 0x8c61037;
static const uintptr_t kChrome146DelegateEncodeCfiCheckOffset = 0x8c624a5;
static const uintptr_t kChrome146DelegateEncodeJobCfiCheckOffset = 0x8c6428c;
static const uintptr_t kChrome146DelegatePrepareEncodeJobCfiCheckOffset =
    0x8c69fcf;
static const uintptr_t kChrome146DelegateGetMetadataCfiCheckOffset = 0x8c6a35e;
static const uintptr_t kChrome146DelegateMetadataCleanupCfiCheckOffset =
    0x8c6a385;
static const uintptr_t kChrome146CreateEncodeJobOffset = 0x8c64c90;
static const uintptr_t kChrome146InitializeTaskPostDelegateUd2Offset = 0x8c611fe;
static const uintptr_t kChrome146H264ArmStoreEncoderOffset = 0x8c60b73;
static const uintptr_t kChrome146AdapterCopySupportsGpuSharedImagesOffset =
    0x77cbc50;
static const uintptr_t kChrome146SetUpVeaConfigInputFormatNv12Offset =
    0x77cc3a3;
static const uintptr_t kChrome146SetUpVeaConfigStorageTypeOffset = 0x77cc3e9;
static const uintptr_t kChrome146EncodePrepareGpuCpuBranchOffset = 0xdc6d286;
static const uintptr_t kChrome146PrepareGpuFrameAddress = 0xdc6f3b0;
static const uintptr_t kChrome146PrepareCpuFrameAddress = 0xdc6fe90;
static const uintptr_t kChrome146PrepareGpuFrameOffset = 0x77ccdb0;
static const uintptr_t kChrome146PrepareCpuFrameOffset = 0x77cd310;
static const uintptr_t kChrome146PrepareCpuFrameDestCodedSizeLoadOffset =
    0x77cd340;
static const uintptr_t kChrome146PrepareCpuFrameNv12FastPathCmpOffset =
    0x77cd36d;
static const uintptr_t kChrome146PrepareCpuFrameAllocationFormatOffset =
    0x77cd40f;
static const uintptr_t kChrome146PrepareCpuFrameWrapFormatOffset = 0x77cd523;
static const uintptr_t kChrome146ReadOnlyRegionPoolMaybeAllocateOffset =
    0x77cfeb0;
static const uintptr_t kChrome146RequireBitstreamBuffersOffset = 0x77ce460;
static const uintptr_t kChrome146MappableMaybeCreateOffset = 0xdc6dfc0;
static const uintptr_t kChrome146PrepareGpuFrameCallOffsets[] = {
    0xdc6d14e,
    0xdc6d2b8,
    0xdc73500,
    0xdc73676,
};
#define ABS_JUMP_SIZE 13
#define PATCH_SIZE_SHORT 13
#define PATCH_SIZE_H264_DELEGATE_CTOR 15
#define PATCH_SIZE_GET_SUPPORTED_PROFILES 19
#define PATCH_SIZE_CREATE_FOR_VIDEO_CODEC 19
#define SUPPORTED_PROFILE_SIZE 0x60

static FILE* g_log;
void* g_original_trampoline;
void* g_initialize_task_trampoline;
void* g_create_encode_job_trampoline;
void* g_ctor_trampoline;
void* g_mappable_maybe_create_trampoline;
void* g_readonly_pool_maybe_allocate_trampoline;
void* g_prepare_cpu_frame_trampoline;
void* g_require_bitstream_buffers_trampoline;
static void* g_get_supported_profiles_trampoline;
static void* g_create_for_video_codec_trampoline;
static void* g_vaapi_create_trampoline;
static void* g_h264_delegate_ctor_trampoline;
static int g_log_fd = -1;
static __thread int g_next_delegate_should_be_h265;

extern void* _Znwm(size_t size);

static void crash_signal_handler(int sig, siginfo_t* info, void* ucontext) {
#if defined(__x86_64__)
  ucontext_t* uc = (ucontext_t*)ucontext;
  uintptr_t rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
  uintptr_t rsp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
  uintptr_t rax = (uintptr_t)uc->uc_mcontext.gregs[REG_RAX];
  uintptr_t rbx = (uintptr_t)uc->uc_mcontext.gregs[REG_RBX];
  uintptr_t rcx = (uintptr_t)uc->uc_mcontext.gregs[REG_RCX];
  uintptr_t rdx = (uintptr_t)uc->uc_mcontext.gregs[REG_RDX];
  uintptr_t rsi = (uintptr_t)uc->uc_mcontext.gregs[REG_RSI];
  uintptr_t rdi = (uintptr_t)uc->uc_mcontext.gregs[REG_RDI];
  uintptr_t rbp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
  uintptr_t r8 = (uintptr_t)uc->uc_mcontext.gregs[REG_R8];
  uintptr_t r9 = (uintptr_t)uc->uc_mcontext.gregs[REG_R9];
  uintptr_t r10 = (uintptr_t)uc->uc_mcontext.gregs[REG_R10];
  uintptr_t r11 = (uintptr_t)uc->uc_mcontext.gregs[REG_R11];
  uintptr_t r12 = (uintptr_t)uc->uc_mcontext.gregs[REG_R12];
  uintptr_t r13 = (uintptr_t)uc->uc_mcontext.gregs[REG_R13];
  uintptr_t r14 = (uintptr_t)uc->uc_mcontext.gregs[REG_R14];
  uintptr_t r15 = (uintptr_t)uc->uc_mcontext.gregs[REG_R15];
#else
  uintptr_t rip = 0;
  uintptr_t rsp = 0;
  uintptr_t rax = 0, rbx = 0, rcx = 0, rdx = 0, rsi = 0, rdi = 0, rbp = 0;
  uintptr_t r8 = 0, r9 = 0, r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0;
  uintptr_t r15 = 0;
#endif
  char line[256];
  int n = snprintf(line, sizeof(line),
                   "caught signal=%d si_addr=%p rip=%p rsp=%p\n", sig,
                   info ? info->si_addr : NULL, (void*)rip, (void*)rsp);
  if (n > 0 && g_log_fd >= 0) {
    ssize_t ignored =
        write(g_log_fd, line,
              (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
    (void)ignored;
  }
  n = snprintf(line, sizeof(line),
               "regs rax=%p rbx=%p rcx=%p rdx=%p rsi=%p rdi=%p rbp=%p\n",
               (void*)rax, (void*)rbx, (void*)rcx, (void*)rdx, (void*)rsi,
               (void*)rdi, (void*)rbp);
  if (n > 0 && g_log_fd >= 0) {
    ssize_t ignored =
        write(g_log_fd, line,
              (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
    (void)ignored;
  }
  n = snprintf(line, sizeof(line),
               "regs r8=%p r9=%p r10=%p r11=%p r12=%p r13=%p r14=%p r15=%p\n",
               (void*)r8, (void*)r9, (void*)r10, (void*)r11, (void*)r12,
               (void*)r13, (void*)r14, (void*)r15);
  if (n > 0 && g_log_fd >= 0) {
    ssize_t ignored =
        write(g_log_fd, line,
              (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
    (void)ignored;
  }
#if defined(__x86_64__)
  if (g_log_fd >= 0 && rsp != 0) {
    for (int i = 0; i < 16; ++i) {
      uintptr_t value = 0;
      memcpy(&value, (const void*)(rsp + (uintptr_t)i * sizeof(uintptr_t)),
             sizeof(value));
      n = snprintf(line, sizeof(line), "stack[%02d]=%p\n", i, (void*)value);
      if (n > 0) {
        ssize_t ignored =
            write(g_log_fd, line,
                  (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
        (void)ignored;
      }
    }
  }
#endif
  signal(sig, SIG_DFL);
  raise(sig);
}

static void install_crash_logger(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = crash_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGILL, &sa, NULL);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGTRAP, &sa, NULL);
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

static void probe_log(const char* fmt, ...) {
  if (!g_log) {
    const char* path = getenv("CHROME_HEVC_TRAMPOLINE_PROBE_LOG");
    if (!path || !*path) {
      path = "/tmp/chrome-hevc-trampoline-probe.log";
    }
    g_log = fopen(path, "a");
    if (!g_log) {
      return;
    }
    g_log_fd = fileno(g_log);
    setvbuf(g_log, NULL, _IOLBF, 0);
  }

  fprintf(g_log, "[pid=%d type=%s] ", getpid(), process_type());
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
}

static void probe_log_fast(const char* message, size_t len) {
  if (g_log_fd < 0) {
    return;
  }
  ssize_t ignored = write(g_log_fd, message, len);
  (void)ignored;
}

static int can_install_patch_in_this_process(void) {
  const char* type = process_type();
  return strcmp(type, "gpu-process") == 0 || strcmp(type, "zygote") == 0;
}

static int env_flag_enabled_or_default(const char* name, int default_value) {
  const char* value = getenv(name);
  if (!value || !*value) {
    return default_value;
  }
  return strcmp(value, "1") == 0;
}

static uintptr_t env_offset_or_default(const char* name, uintptr_t fallback) {
  const char* value = getenv(name);
  if (!value || !*value) {
    return fallback;
  }
  char* end = NULL;
  errno = 0;
  unsigned long long parsed = strtoull(value, &end, 0);
  if (errno != 0 || end == value || (end && *end != '\0')) {
    probe_log("invalid offset override %s=%s", name, value);
    return fallback;
  }
  probe_log("offset override %s=0x%llx", name, parsed);
  return (uintptr_t)parsed;
}

static int find_chrome_text_base(uintptr_t* base_out) {
  const char* expected_path = getenv("CHROME_HEVC_TRAMPOLINE_CHROME_PATH");
  FILE* f = fopen("/proc/self/maps", "r");
  if (!f) {
    probe_log("failed opening maps: %s", strerror(errno));
    return 0;
  }

  char line[4096];
  while (fgets(line, sizeof(line), f)) {
    uintptr_t start = 0;
    uintptr_t end = 0;
    uintptr_t offset = 0;
    char perms[8] = {0};
    char path[3072] = {0};
    int fields = sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %" SCNxPTR
                              " %*s %*s %3071s",
                        &start, &end, perms, &offset, path);
    if (fields < 5) {
      continue;
    }
    int path_matches = 0;
    if (expected_path && *expected_path) {
      path_matches = strcmp(path, expected_path) == 0;
    } else {
      path_matches = strstr(path, "/opt/google/chrome/chrome") != NULL;
    }
    if (!path_matches) {
      continue;
    }
    if (strchr(perms, 'x') == NULL) {
      continue;
    }
    *base_out = start - offset;
    fclose(f);
    return 1;
  }

  fclose(f);
  probe_log("chrome executable mapping not found");
  return 0;
}

static size_t write_abs_jump(uint8_t* at, void* target) {
  // movabs r11, imm64; jmp r11.  Do not clobber rax: some copied prologues
  // keep a live value there before the trampoline tail jump.
  at[0] = 0x49;
  at[1] = 0xBB;
  memcpy(at + 2, &target, sizeof(target));
  at[10] = 0x41;
  at[11] = 0xFF;
  at[12] = 0xE3;
  return ABS_JUMP_SIZE;
}

static int install_abs_jump_hook(uint8_t* target,
                                 const uint8_t* expected,
                                 size_t patch_size,
                                 void* hook,
                                 void** trampoline_out,
                                 const char* name);

typedef void* (*MappableMaybeCreateFn)(void* self, void* visible_size);
typedef void* (*ReadOnlyPoolMaybeAllocateFn)(void* self);
typedef void* (*PrepareFrameFn)(void* ret, void* self, void* frame);
typedef void (*RequireBitstreamBuffersFn)(void* self,
                                          unsigned int input_count,
                                          const void* input_coded_size,
                                          size_t output_buffer_size);
typedef void* (*CreateEncodeJobFn)(void* self,
                                   uintptr_t a1,
                                   uintptr_t a2,
                                   uintptr_t a3,
                                   uintptr_t a4,
                                   uintptr_t a5,
                                   uintptr_t a6);

__attribute__((used, noinline)) static void* mappable_maybe_create_hook(
    void* self,
    void* visible_size) {
  void* caller = __builtin_return_address(0);
  char line[192];
  int n = snprintf(line, sizeof(line),
                   "hit MappableSharedImage MaybeCreate self=%p size_arg=%p "
                   "caller=%p\n",
                   self, visible_size, caller);
  if (n > 0 && g_log_fd >= 0) {
    ssize_t ignored =
        write(g_log_fd, line,
              (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
    (void)ignored;
  }
  MappableMaybeCreateFn original =
      (MappableMaybeCreateFn)g_mappable_maybe_create_trampoline;
  return original(self, visible_size);
}

__attribute__((used, noinline)) static void* readonly_pool_maybe_allocate_hook(
    void* self) {
  size_t buffer_size = 0;
  uintptr_t begin = 0;
  uintptr_t end = 0;
  uintptr_t cap = 0;
  if (self) {
    memcpy(&buffer_size, (const uint8_t*)self + 0x8, sizeof(buffer_size));
    memcpy(&begin, (const uint8_t*)self + 0x10, sizeof(begin));
    memcpy(&end, (const uint8_t*)self + 0x18, sizeof(end));
    memcpy(&cap, (const uint8_t*)self + 0x20, sizeof(cap));
  }
  probe_log("hit ReadOnlyRegionPool::MaybeAllocateBuffer self=%p size=%zu "
            "begin=%p end=%p cap=%p",
            self, buffer_size, (void*)begin, (void*)end, (void*)cap);
  ReadOnlyPoolMaybeAllocateFn original =
      (ReadOnlyPoolMaybeAllocateFn)g_readonly_pool_maybe_allocate_trampoline;
  void* result = original(self);
  probe_log("ReadOnlyRegionPool::MaybeAllocateBuffer result=%p", result);
  return result;
}

static int install_mappable_maybe_create_hook(uint8_t* target) {
  uint8_t expected[14] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x56,                   // push r14
      0x53,                         // push rbx
      0x48, 0x81, 0xEC, 0x60, 0x01, // sub rsp, 0x160
      0x00, 0x00,
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)mappable_maybe_create_hook,
                               &g_mappable_maybe_create_trampoline,
                               "MappableSharedImage MaybeCreate");
}

static int install_readonly_pool_maybe_allocate_hook(uint8_t* target) {
  uint8_t expected[20] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
      0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00,
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)readonly_pool_maybe_allocate_hook,
                               &g_readonly_pool_maybe_allocate_trampoline,
                               "ReadOnlyRegionPool::MaybeAllocateBuffer");
}

__attribute__((used, noinline)) static void* prepare_cpu_frame_hook(
    void* ret,
    void* self,
    void* frame) {
  int input_format = -1;
  int profile = -1;
  uintptr_t input_pool = 0;
  uintptr_t input_coded_size = 0;
  if (self) {
    memcpy(&input_format, (const uint8_t*)self + 0x12c, sizeof(input_format));
    memcpy(&profile, (const uint8_t*)self + 0x128, sizeof(profile));
    memcpy(&input_pool, (const uint8_t*)self + 0x18, sizeof(input_pool));
    memcpy(&input_coded_size, (const uint8_t*)self + 0x1b0,
           sizeof(input_coded_size));
  }
  probe_log("hit PrepareCpuFrame ret=%p self=%p frame=%p profile=%d "
            "input_format=%d input_pool=%p input_coded_size_packed=0x%016" PRIxPTR,
            ret, self, frame, profile, input_format, (void*)input_pool,
            input_coded_size);

  PrepareFrameFn original = (PrepareFrameFn)g_prepare_cpu_frame_trampoline;
  void* out = original(ret, self, frame);
  if (ret) {
    uintptr_t q0 = 0;
    uintptr_t q1 = 0;
    uintptr_t q2 = 0;
    uintptr_t q3 = 0;
    memcpy(&q0, (const uint8_t*)ret + 0x0, sizeof(q0));
    memcpy(&q1, (const uint8_t*)ret + 0x8, sizeof(q1));
    memcpy(&q2, (const uint8_t*)ret + 0x10, sizeof(q2));
    memcpy(&q3, (const uint8_t*)ret + 0x18, sizeof(q3));
    probe_log("PrepareCpuFrame returned out=%p raw=%016" PRIxPTR
              " %016" PRIxPTR " %016" PRIxPTR " %016" PRIxPTR,
              out, q0, q1, q2, q3);
  } else {
    probe_log("PrepareCpuFrame returned out=%p ret=null", out);
  }
  return out;
}

static int install_prepare_cpu_frame_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_SHORT] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)prepare_cpu_frame_hook,
                               &g_prepare_cpu_frame_trampoline,
                               "PrepareCpuFrame");
}

__attribute__((used, noinline)) static void require_bitstream_buffers_hook(
    void* self,
    unsigned int input_count,
    const void* input_coded_size,
    size_t output_buffer_size) {
  int state = -1;
  int profile = -1;
  int input_format = -1;
  int uses_cpu_input_buffers = -1;
  uintptr_t output_pool = 0;
  uintptr_t output_begin = 0;
  uintptr_t output_end = 0;
  uintptr_t output_cap = 0;
  uintptr_t old_input_coded_size = 0;
  uintptr_t requested_size = 0;

  if (input_coded_size) {
    memcpy(&requested_size, input_coded_size, sizeof(requested_size));
  }
  if (self) {
    memcpy(&output_pool, (const uint8_t*)self + 0x20, sizeof(output_pool));
    memcpy(&output_begin, (const uint8_t*)self + 0x28, sizeof(output_begin));
    memcpy(&output_end, (const uint8_t*)self + 0x30, sizeof(output_end));
    memcpy(&output_cap, (const uint8_t*)self + 0x38, sizeof(output_cap));
    memcpy(&state, (const uint8_t*)self + 0xf8, sizeof(state));
    memcpy(&profile, (const uint8_t*)self + 0x128, sizeof(profile));
    memcpy(&input_format, (const uint8_t*)self + 0x12c,
           sizeof(input_format));
    memcpy(&uses_cpu_input_buffers, (const uint8_t*)self + 0x130,
           sizeof(uses_cpu_input_buffers));
    memcpy(&old_input_coded_size, (const uint8_t*)self + 0x1b0,
           sizeof(old_input_coded_size));
  }
  if (self && env_flag_enabled_or_default("CHROME_HEVC_TRAMPOLINE_SCAN_VEA", 0)) {
    for (size_t off = 0; off < 0x220; off += 4) {
      uint32_t value = 0;
      memcpy(&value, (const uint8_t*)self + off, sizeof(value));
      if (value == 8 || value == 16 || value == 1520 || value == 608 ||
          value == 4099 || value == 1 || value == 2) {
        probe_log("VEA scan self+0x%zx = 0x%08x (%u)", off, value, value);
      }
    }
  }

  probe_log("hit RequireBitstreamBuffers self=%p input_count=%u "
            "requested_size_packed=0x%016" PRIxPTR
            " output_buffer_size=%zu state=%d profile=%d input_format=%d "
            "uses_cpu=%d output_pool=%p output_handles=%p/%p/%p "
            "old_input_coded_size=0x%016" PRIxPTR,
            self, input_count, requested_size, output_buffer_size, state,
            profile, input_format, uses_cpu_input_buffers, (void*)output_pool,
            (void*)output_begin, (void*)output_end, (void*)output_cap,
            old_input_coded_size);

  if (env_flag_enabled_or_default(
          "CHROME_HEVC_TRAMPOLINE_FIX_REQUIRE_BITSTREAM_ARGS", 0) &&
      output_buffer_size == 0 && input_count >= 4096) {
    const char* input_count_env = getenv("CHROME_HEVC_SHIM_INPUT_COUNT");
    output_buffer_size = input_count;
    input_count =
        input_count_env && *input_count_env ? (unsigned int)atoi(input_count_env)
                                            : 2u;
    if (input_count == 0) {
      input_count = 2;
    }
    probe_log("fixed RequireBitstreamBuffers args input_count=%u "
              "output_buffer_size=%zu",
              input_count, output_buffer_size);
  }
  if (env_flag_enabled_or_default(
          "CHROME_HEVC_TRAMPOLINE_FIX_REQUIRE_BITSTREAM_ARGS", 0) &&
      output_buffer_size == 0 && input_count > 0 && input_count < 64 &&
      requested_size != 0) {
    const char* input_count_env = getenv("CHROME_HEVC_SHIM_INPUT_COUNT");
    const char* output_size_env =
        getenv("CHROME_HEVC_SHIM_BITSTREAM_BUFFER_SIZE");
    unsigned int fixed_input_count =
        input_count_env && *input_count_env ? (unsigned int)atoi(input_count_env)
                                            : input_count;
    size_t fixed_output_size =
        output_size_env && *output_size_env ? (size_t)strtoull(output_size_env, NULL, 0)
                                            : 524288u;
    if (fixed_input_count == 0) {
      fixed_input_count = 2;
    }
    if (fixed_output_size == 0) {
      fixed_output_size = 524288u;
    }
    input_count = fixed_input_count;
    output_buffer_size = fixed_output_size;
    probe_log("fixed RequireBitstreamBuffers zero output size "
              "input_count=%u output_buffer_size=%zu",
              input_count, output_buffer_size);
  }

  RequireBitstreamBuffersFn original =
      (RequireBitstreamBuffersFn)g_require_bitstream_buffers_trampoline;
  original(self, input_count, input_coded_size, output_buffer_size);

  if (self) {
    memcpy(&output_begin, (const uint8_t*)self + 0x28, sizeof(output_begin));
    memcpy(&output_end, (const uint8_t*)self + 0x30, sizeof(output_end));
    memcpy(&output_cap, (const uint8_t*)self + 0x38, sizeof(output_cap));
    memcpy(&state, (const uint8_t*)self + 0xf8, sizeof(state));
    memcpy(&old_input_coded_size, (const uint8_t*)self + 0x1b0,
           sizeof(old_input_coded_size));
  }
  probe_log("RequireBitstreamBuffers returned state=%d "
            "output_handles=%p/%p/%p input_coded_size=0x%016" PRIxPTR,
            state, (void*)output_begin, (void*)output_end, (void*)output_cap,
            old_input_coded_size);
}

static int install_require_bitstream_buffers_hook(uint8_t* target) {
  uint8_t expected[20] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
      0x48, 0x81, 0xEC, 0x78, 0x01, 0x00, 0x00,
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)require_bitstream_buffers_hook,
                               &g_require_bitstream_buffers_trampoline,
                               "RequireBitstreamBuffers");
}

__attribute__((used, noinline)) static void* create_encode_job_hook(
    void* self,
    uintptr_t a1,
    uintptr_t a2,
    uintptr_t a3,
    uintptr_t a4,
    uintptr_t a5,
    uintptr_t a6) {
  uint32_t output_codec = 0;
  uint64_t expected_size = 0;
  uint64_t visible_a = 0;
  uint64_t visible_b = 0;
  if (self) {
    memcpy(&expected_size, (const uint8_t*)self + 0x110, sizeof(expected_size));
    memcpy(&output_codec, (const uint8_t*)self + 0x118, sizeof(output_codec));
    memcpy(&visible_a, (const uint8_t*)self + 0x11c, sizeof(visible_a));
    memcpy(&visible_b, (const uint8_t*)self + 0x124, sizeof(visible_b));
  }
  probe_log("hit CreateEncodeJob self=%p args=%" PRIxPTR "/%" PRIxPTR
            "/%" PRIxPTR "/%" PRIxPTR
            "/%" PRIxPTR "/%" PRIxPTR
            " output_codec=%u expected=0x%016" PRIx64
            " visible=0x%016" PRIx64 "/0x%016" PRIx64,
            self, a1, a2, a3, a4, a5, a6, output_codec, expected_size,
            visible_a, visible_b);
  CreateEncodeJobFn original =
      (CreateEncodeJobFn)g_create_encode_job_trampoline;
  int restored_codec = 0;
  if (self && output_codec == 8 &&
      env_flag_enabled_or_default(
          "CHROME_HEVC_TRAMPOLINE_CREATE_JOB_AS_H264", 0)) {
    uint32_t h264_codec = 1;
    memcpy((uint8_t*)self + 0x118, &h264_codec, sizeof(h264_codec));
    restored_codec = 1;
    probe_log("CreateEncodeJob temporarily patched output_codec HEVC->H264");
  }
  void* ret = original(self, a1, a2, a3, a4, a5, a6);
  if (self && restored_codec) {
    memcpy((uint8_t*)self + 0x118, &output_codec, sizeof(output_codec));
    probe_log("CreateEncodeJob restored output_codec=%u", output_codec);
  }
  probe_log("CreateEncodeJob returned %p", ret);
  return ret;
}

static int install_create_encode_job_hook(uint8_t* target) {
  uint8_t expected[17] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
      0x48, 0x83, 0xEC, 0x78,       // sub rsp, 0x78
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)create_encode_job_hook,
                               &g_create_encode_job_trampoline,
                               "CreateEncodeJob");
}

__attribute__((used, noinline)) static void on_vea_initialize_hit(void) {
  static const char kMessage[] = "hit VEA initialize trampoline\n";
  probe_log_fast(kMessage, sizeof(kMessage) - 1);
}

__attribute__((naked)) static void vea_initialize_hook(void) {
  __asm__ volatile(
      "push %rax\n"
      "push %rcx\n"
      "push %rdx\n"
      "push %rsi\n"
      "push %rdi\n"
      "push %r8\n"
      "push %r9\n"
      "push %r10\n"
      "push %r11\n"
      "call on_vea_initialize_hit\n"
      "pop %r11\n"
      "pop %r10\n"
      "pop %r9\n"
      "pop %r8\n"
      "pop %rdi\n"
      "pop %rsi\n"
      "pop %rdx\n"
      "pop %rcx\n"
      "pop %rax\n"
      "mov g_original_trampoline@GOTPCREL(%rip), %r11\n"
      "mov (%r11), %r11\n"
      "jmp *%r11\n");
}

__attribute__((used, noinline)) static void on_vea_initialize_task_hit(void) {
  static const char kMessage[] = "hit VEA initialize task trampoline\n";
  probe_log_fast(kMessage, sizeof(kMessage) - 1);
}

__attribute__((naked)) static void vea_initialize_task_hook(void) {
  __asm__ volatile(
      "pushfq\n"
      "push %rax\n"
      "push %rcx\n"
      "push %rdx\n"
      "push %rsi\n"
      "push %rdi\n"
      "push %r8\n"
      "push %r9\n"
      "push %r10\n"
      "push %r11\n"
      "call on_vea_initialize_task_hit\n"
      "pop %r11\n"
      "pop %r10\n"
      "pop %r9\n"
      "pop %r8\n"
      "pop %rdi\n"
      "pop %rsi\n"
      "pop %rdx\n"
      "pop %rcx\n"
      "pop %rax\n"
      "popfq\n"
      "mov g_initialize_task_trampoline@GOTPCREL(%rip), %r11\n"
      "mov (%r11), %r11\n"
      "jmp *%r11\n");
}

__attribute__((used, noinline)) static void on_vea_ctor_hit(void) {
  static const char kMessage[] = "hit VEA constructor trampoline\n";
  probe_log_fast(kMessage, sizeof(kMessage) - 1);
}

__attribute__((naked)) static void vea_ctor_hook(void) {
  __asm__ volatile(
      "push %rax\n"
      "push %rcx\n"
      "push %rdx\n"
      "push %rsi\n"
      "push %rdi\n"
      "push %r8\n"
      "push %r9\n"
      "push %r10\n"
      "push %r11\n"
      "call on_vea_ctor_hit\n"
      "pop %r11\n"
      "pop %r10\n"
      "pop %r9\n"
      "pop %r8\n"
      "pop %rdi\n"
      "pop %rsi\n"
      "pop %rdx\n"
      "pop %rcx\n"
      "pop %rax\n"
      "mov g_ctor_trampoline@GOTPCREL(%rip), %r11\n"
      "mov (%r11), %r11\n"
      "jmp *%r11\n");
}

static int install_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_SHORT] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)vea_initialize_hook,
                               &g_original_trampoline, "VEA initialize");
}

static int install_initialize_task_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_SHORT] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)vea_initialize_task_hook,
                               &g_initialize_task_trampoline,
                               "VEA initialize task");
}

static int install_abs_jump_hook(uint8_t* target,
                                 const uint8_t* expected,
                                 size_t patch_size,
                                 void* hook,
                                 void** trampoline_out,
                                 const char* name) {
  if (patch_size < ABS_JUMP_SIZE) {
    probe_log("%s patch size is too small: %zu", name, patch_size);
    return 0;
  }
  if (memcmp(target, expected, patch_size) != 0) {
    probe_log("%s prologue mismatch at %p", name, target);
    char actual[256];
    char want[256];
    size_t pos_actual = 0;
    size_t pos_want = 0;
    size_t dump = patch_size < 32 ? patch_size : 32;
    for (size_t i = 0; i < dump && pos_actual + 4 < sizeof(actual); ++i) {
      pos_actual += (size_t)snprintf(actual + pos_actual,
                                    sizeof(actual) - pos_actual,
                                    "%02x%s", target[i],
                                    i + 1 == dump ? "" : " ");
      pos_want += (size_t)snprintf(want + pos_want,
                                  sizeof(want) - pos_want,
                                  "%02x%s", expected[i],
                                  i + 1 == dump ? "" : " ");
    }
    probe_log("%s actual=%s", name, actual);
    probe_log("%s expect=%s", name, want);
    return 0;
  }
  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uint8_t* trampoline =
      mmap(NULL, page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (trampoline == MAP_FAILED) {
    probe_log("mmap trampoline failed: %s", strerror(errno));
    return 0;
  }

  memcpy(trampoline, target, patch_size);
  write_abs_jump(trampoline + patch_size, target + patch_size);
  __builtin___clear_cache((char*)trampoline,
                          (char*)trampoline + patch_size + ABS_JUMP_SIZE);
  *trampoline_out = trampoline;

  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect target failed: %s", strerror(errno));
    return 0;
  }

  memset(target, 0x90, patch_size);
  write_abs_jump(target, hook);
  __builtin___clear_cache((char*)target, (char*)target + patch_size);
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);

  probe_log("installed %s trampoline target=%p trampoline=%p hook=%p", name,
            target, trampoline, hook);
  return 1;
}

__attribute__((used, noinline)) static void on_get_supported_profiles_hit(void) {
  static const char kMessage[] = "hit VEA GetSupportedProfiles trampoline\n";
  probe_log_fast(kMessage, sizeof(kMessage) - 1);
}

__attribute__((naked)) static void get_supported_profiles_hook(void) {
  __asm__ volatile(
      "push %rdi\n"
      "mov g_get_supported_profiles_trampoline@GOTPCREL(%rip), %r11\n"
      "mov (%r11), %r11\n"
      "call *%r11\n"
      "push %rax\n"
      "sub $8, %rsp\n"
      "mov 16(%rsp), %rdi\n"
      "call postprocess_supported_profiles\n"
      "add $8, %rsp\n"
      "pop %rax\n"
      "add $8, %rsp\n"
      "ret\n");
}

struct StdVectorLayout {
  uint8_t* begin;
  uint8_t* end;
  uint8_t* cap;
};

__attribute__((used, noinline)) static void postprocess_supported_profiles(
    void* ret) {
  static const char kHit[] = "hit VEA GetSupportedProfiles postprocess\n";
  probe_log_fast(kHit, sizeof(kHit) - 1);

  struct StdVectorLayout* vec = (struct StdVectorLayout*)ret;
  if (!vec || !vec->begin || vec->end < vec->begin || vec->cap < vec->begin) {
    static const char kBad[] = "VEA profiles vector invalid\n";
    probe_log_fast(kBad, sizeof(kBad) - 1);
    return;
  }

  size_t count = (size_t)(vec->end - vec->begin) / SUPPORTED_PROFILE_SIZE;
  size_t capacity = (size_t)(vec->cap - vec->begin) / SUPPORTED_PROFILE_SIZE;
  int has_hevc_main = 0;
  int source_index = -1;

  for (size_t i = 0; i < count; ++i) {
    uint8_t* entry = vec->begin + i * SUPPORTED_PROFILE_SIZE;
    int profile = *(int*)entry;
    if (profile == 16) {
      has_hevc_main = 1;
    }
    char entry_line[192];
    int entry_n = snprintf(entry_line, sizeof(entry_line),
                           "VEA profile[%zu] profile=%d a=%d b=%d c=%d d=%d "
                           "rate_modes=0x%02x\n",
                           i, profile, *(int*)(entry + 4),
                           *(int*)(entry + 8), *(int*)(entry + 12),
                           *(int*)(entry + 16), entry[28]);
    if (entry_n > 0) {
      ssize_t ignored =
          write(g_log_fd, entry_line,
                (size_t)(entry_n < (int)sizeof(entry_line)
                             ? entry_n
                             : (int)sizeof(entry_line)));
      (void)ignored;
    }
    if (source_index < 0 && profile >= 1 && profile <= 3) {
      source_index = (int)i;
    }
  }

  uint8_t source_rate_modes = 0;
  if (source_index >= 0) {
    uint8_t* source = vec->begin + (size_t)source_index * SUPPORTED_PROFILE_SIZE;
    source_rate_modes = source[28];
  }

  char line[192];
  int n = snprintf(line, sizeof(line),
                   "VEA profiles count=%zu capacity=%zu has_hevc=%d "
                   "source=%d source_rate_modes=0x%02x\n",
                   count, capacity, has_hevc_main, source_index,
                   source_rate_modes);
  if (n > 0) {
    ssize_t ignored = write(g_log_fd, line,
                            (size_t)(n < (int)sizeof(line) ? n
                                                           : (int)sizeof(line)));
    (void)ignored;
  }

  const char* spoof = getenv("CHROME_HEVC_TRAMPOLINE_SPOOF_PROFILE");
  if (!spoof || strcmp(spoof, "1") != 0 || has_hevc_main || source_index < 0) {
    return;
  }
  if (count >= capacity) {
    size_t new_capacity = count + 1;
    uint8_t* new_begin = (uint8_t*)_Znwm(new_capacity * SUPPORTED_PROFILE_SIZE);
    memcpy(new_begin, vec->begin, count * SUPPORTED_PROFILE_SIZE);
    // Leak the old vector buffer intentionally.  The raw-copied elements own
    // nested std::vector storage; destroying both old and new elements would
    // double-free those nested buffers.
    vec->begin = new_begin;
    vec->end = new_begin + count * SUPPORTED_PROFILE_SIZE;
    vec->cap = new_begin + new_capacity * SUPPORTED_PROFILE_SIZE;
    static const char kGrew[] = "grew VEA profiles vector by leaking old buffer\n";
    probe_log_fast(kGrew, sizeof(kGrew) - 1);
  }

  uint8_t* source = vec->begin + (size_t)source_index * SUPPORTED_PROFILE_SIZE;
  uint8_t* dest = vec->end;
  memcpy(dest, source, SUPPORTED_PROFILE_SIZE);
  *(int*)dest = 16;  // media::HEVCPROFILE_MAIN
  const char* rate_modes = getenv("CHROME_HEVC_TRAMPOLINE_RATE_MODES");
  if (rate_modes && strcmp(rate_modes, "constant") == 0) {
    dest[28] = 1;  // VideoEncodeAccelerator::kConstantMode only.
  } else if (rate_modes && strcmp(rate_modes, "all") == 0) {
    dest[28] = 7;  // Constant, variable, and external/source-backed modes.
  }
  // Clear nested std::vector fields copied from the source profile.  Offsets
  // match Chrome 146's VideoEncodeAccelerator::SupportedProfile layout.
  memset(dest + 32, 0, 24);  // scalability_modes
  memset(dest + 64, 0, 24);  // gpu_supported_pixel_formats
  dest[88] = 0;              // supports_gpu_shared_images
  vec->end += SUPPORTED_PROFILE_SIZE;
  static const char kSpoofed[] = "spoofed HEVC Main supported profile\n";
  probe_log_fast(kSpoofed, sizeof(kSpoofed) - 1);
}

static int install_get_supported_profiles_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_GET_SUPPORTED_PROFILES] = {
      0x55,                          // push rbp
      0x48, 0x89, 0xE5,              // mov rsp, rbp
      0x53,                          // push rbx
      0x50,                          // push rax
      0x48, 0x89, 0xF0,              // mov rsi, rax
      0x48, 0x89, 0xFB,              // mov rdi, rbx
      0x48, 0x8B, 0xB6, 0x28, 0x03,  // mov 0x328(rsi), rsi
      0x00, 0x00,
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)get_supported_profiles_hook,
                               &g_get_supported_profiles_trampoline,
                               "VEA GetSupportedProfiles");
}

typedef void* (*CreateForVideoCodecFn)(void* ret,
                                       int mode,
                                       int profile,
                                       int encryption_scheme,
                                       void* report_error_cb);

__attribute__((used, noinline)) static void* create_for_video_codec_hook(
    void* ret,
    int mode,
    int profile,
    int encryption_scheme,
    void* report_error_cb) {
  CreateForVideoCodecFn original =
      (CreateForVideoCodecFn)g_create_for_video_codec_trampoline;
  void* out = original(ret, mode, profile, encryption_scheme, report_error_cb);

  unsigned char status = ret ? *(unsigned char*)ret : 0xff;
  char line[192];
  int n = snprintf(line, sizeof(line),
                   "CreateForVideoCodec mode=%d profile=%d status=%u\n", mode,
                   profile, status);
  if (n > 0) {
    ssize_t ignored = write(g_log_fd, line,
                            (size_t)(n < (int)sizeof(line) ? n
                                                           : (int)sizeof(line)));
    (void)ignored;
  }

  const char* retry = getenv("CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP");
  if (ret && status != 0 && profile == 16 && mode != 2 && retry &&
      strcmp(retry, "1") == 0) {
    static const char kRetry[] = "retrying HEVC CreateForVideoCodec as CQP\n";
    probe_log_fast(kRetry, sizeof(kRetry) - 1);
    out = original(ret, 2, profile, encryption_scheme, report_error_cb);
    status = *(unsigned char*)ret;
    n = snprintf(line, sizeof(line),
                 "CreateForVideoCodec HEVC CQP retry status=%u\n", status);
    if (n > 0) {
      ssize_t ignored =
          write(g_log_fd, line,
                (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
      (void)ignored;
    }
  }

  const char* replace_delegate =
      getenv("CHROME_HEVC_TRAMPOLINE_REPLACE_H264_DELEGATE_WITH_H265");
  if (ret && status == 0 && profile == 16 && replace_delegate &&
      strcmp(replace_delegate, "1") == 0) {
    g_next_delegate_should_be_h265 = 1;
    static const char kNext[] = "next H264 delegate ctor will construct H265\n";
    probe_log_fast(kNext, sizeof(kNext) - 1);
  }

  return out;
}

static int install_create_for_video_codec_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_CREATE_FOR_VIDEO_CODEC] = {
      0x55,                          // push rbp
      0x48, 0x89, 0xE5,              // mov rsp, rbp
      0x41, 0x57,                    // push r15
      0x41, 0x56,                    // push r14
      0x53,                          // push rbx
      0x50,                          // push rax
      0x4C, 0x89, 0xC3,              // mov r8, rbx
      0x41, 0x89, 0xF6,              // mov esi, r14d
      0x49, 0x89, 0xFF,              // mov rdi, r15
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)create_for_video_codec_hook,
                               &g_create_for_video_codec_trampoline,
                               "VaapiWrapper CreateForVideoCodec");
}

typedef void* (*VaapiCreateFn)(void* ret,
                               int mode,
                               int va_profile,
                               int encryption_scheme,
                               void* report_error_cb);

__attribute__((used, noinline)) static void* vaapi_create_hook(
    void* ret,
    int mode,
    int va_profile,
    int encryption_scheme,
    void* report_error_cb) {
  VaapiCreateFn original = (VaapiCreateFn)g_vaapi_create_trampoline;
  void* out = original(ret, mode, va_profile, encryption_scheme, report_error_cb);
  unsigned char status = ret ? *(unsigned char*)ret : 0xff;
  char line[192];
  int n = snprintf(line, sizeof(line),
                   "VaapiWrapper::Create mode=%d va_profile=%d status=%u\n",
                   mode, va_profile, status);
  if (n > 0) {
    ssize_t ignored = write(g_log_fd, line,
                            (size_t)(n < (int)sizeof(line) ? n
                                                           : (int)sizeof(line)));
    (void)ignored;
  }

  const char* retry = getenv("CHROME_HEVC_TRAMPOLINE_RETRY_HEVC_CQP");
  if (ret && status != 0 && va_profile == 17 && mode != 2 && retry &&
      strcmp(retry, "1") == 0) {
    static const char kRetry[] = "retrying HEVC VaapiWrapper::Create as CQP\n";
    probe_log_fast(kRetry, sizeof(kRetry) - 1);
    out = original(ret, 2, va_profile, encryption_scheme, report_error_cb);
    status = *(unsigned char*)ret;
    n = snprintf(line, sizeof(line),
                 "VaapiWrapper::Create HEVC CQP retry status=%u\n", status);
    if (n > 0) {
      ssize_t ignored =
          write(g_log_fd, line,
                (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
      (void)ignored;
    }
  }
  return out;
}

static int install_vaapi_create_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_SHORT] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)vaapi_create_hook,
                               &g_vaapi_create_trampoline,
                               "VaapiWrapper Create");
}

static int patch_accept_hevc_in_mode_selection(uint8_t* target) {
  uint8_t expected[3] = {0x83, 0xF9, 0x02};  // cmp ecx, 2
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("HEVC mode-selection cmp mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect HEVC mode-selection cmp failed: %s", strerror(errno));
    return 0;
  }
  target[2] = 0x03;  // Accept codec enum values 6, 7, and 8.
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched HEVC mode-selection cmp target=%p", target);
  return 1;
}

static int patch_accept_hevc_in_initialize_codec_mask(uint8_t* target) {
  uint8_t expected[5] = {
      0xB8, 0xC2, 0x04, 0x00, 0x00,  // mov eax, 0x4c2
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("HEVC initialize codec-mask mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect HEVC initialize codec-mask failed: %s",
              strerror(errno));
    return 0;
  }
  target[1] = 0xC2;  // Keep existing H264/VP8/VP9/AV1 bits.
  target[2] = 0x05;  // Add bit 8 for media::VideoCodec::kHEVC: 0x4c2 -> 0x5c2.
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched HEVC initialize codec-mask target=%p", target);
  return 1;
}

static int patch_bypass_initialize_vbr_restriction(uint8_t* target) {
  if (target[0] != 0x83 || target[1] != 0x7B || target[2] != 0x10 ||
      target[3] != 0x01 || target[4] != 0x75) {
    probe_log("HEVC initialize VBR restriction mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect HEVC initialize VBR restriction failed: %s",
              strerror(errno));
    return 0;
  }
  uint8_t patch[6] = {
      0xEB, (uint8_t)(target[5] + 4),  // jmp to the original jne target
      0x90, 0x90, 0x90, 0x90,
  };
  memcpy(target, patch, sizeof(patch));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(patch));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched HEVC initialize VBR restriction target=%p", target);
  return 1;
}

static int patch_bypass_vaapi_profile_table_miss(uint8_t* target) {
  uint8_t expected[6] = {
      0x0F, 0x84, 0x47, 0x01, 0x00, 0x00,  // je unsupported-profile
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("VaapiWrapper profile-table miss bypass mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect VaapiWrapper profile-table miss bypass failed: %s",
              strerror(errno));
    return 0;
  }
  uint8_t patch[6] = {
      0x0F, 0x84, 0xC3, 0x00, 0x00, 0x00,  // je create-handle-then-wrapper
  };
  memcpy(target, patch, sizeof(patch));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(patch));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched VaapiWrapper profile-table miss bypass target=%p", target);
  return 1;
}

static int patch_default_encode_mode(uint8_t* target, uint8_t mode) {
  uint8_t expected[6] = {
      0x41, 0xBF, 0x02, 0x00, 0x00, 0x00,  // mov r15d, 2
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("default encode mode patch mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect default encode mode failed: %s", strerror(errno));
    return 0;
  }
  target[2] = mode;
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched default encode mode target=%p mode=%u", target, mode);
  return 1;
}

static int patch_force_mode_selection_accept(uint8_t* target) {
  if (target[0] != 0x83 || target[1] != 0xF8 || target[2] != 0x01 ||
      target[3] != 0x0F || target[4] != 0x85) {
    probe_log("force mode-selection accept mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect force mode-selection accept failed: %s",
              strerror(errno));
    return 0;
  }
  uint8_t patch[9] = {
      0xE9, 0x22, 0x00, 0x00, 0x00,  // jmp accept-mode block
      0x90, 0x90, 0x90, 0x90,
  };
  memcpy(target, patch, sizeof(patch));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(patch));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched force mode-selection accept target=%p", target);
  return 1;
}

static int patch_delegate_switch_hevc_to_h264(uint8_t* target) {
  uint8_t expected[4] = {
      0x3E, 0xA7, 0x8F, 0x06,  // HEVC entry currently jumps to int3/ud2.
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("delegate switch HEVC entry mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE) != 0) {
    probe_log("mprotect delegate switch HEVC entry failed: %s",
              strerror(errno));
    return 0;
  }
  uint8_t patch[4] = {
      0x12, 0x9E, 0x8F, 0x06,  // Route to the H264 delegate constructor arm.
  };
  memcpy(target, patch, sizeof(patch));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(patch));
  mprotect((void*)page, page_size, PROT_READ);
  probe_log("patched delegate switch HEVC entry to H264 arm target=%p", target);
  return 1;
}

static int patch_delegate_switch_hevc_to_h264_from_table(uint8_t* table) {
  uint8_t* h264_entry = table;
  uint8_t* hevc_entry = table + 0x1c;
  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)hevc_entry & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE) != 0) {
    probe_log("mprotect delegate switch table failed: %s", strerror(errno));
    return 0;
  }
  uint8_t original[4];
  uint8_t replacement[4];
  memcpy(original, hevc_entry, sizeof(original));
  memcpy(replacement, h264_entry, sizeof(replacement));
  memcpy(hevc_entry, replacement, sizeof(replacement));
  __builtin___clear_cache((char*)hevc_entry,
                          (char*)hevc_entry + sizeof(replacement));
  mprotect((void*)page, page_size, PROT_READ);
  probe_log("patched delegate switch table=%p h264=%02x %02x %02x %02x "
            "hevc_old=%02x %02x %02x %02x",
            table, replacement[0], replacement[1], replacement[2],
            replacement[3], original[0], original[1], original[2],
            original[3]);
  return 1;
}

static int patch_nop_cfi_branch(uint8_t* target, const char* name) {
  uint8_t expected[6] = {
      0x0F, 0x87,  // ja cfi-trap
      target[2], target[3], target[4], target[5],
  };
  if (target[0] != expected[0] || target[1] != expected[1]) {
    probe_log("%s CFI branch mismatch at %p", name, target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect %s CFI branch failed: %s", name, strerror(errno));
    return 0;
  }
  memset(target, 0x90, sizeof(expected));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched %s CFI branch target=%p", name, target);
  return 1;
}

static int patch_nop_cfi_branch_optional(uint8_t* target, const char* name) {
  if (target[0] == 0x0F && target[1] == 0x87) {
    return patch_nop_cfi_branch(target, name);
  }
  probe_log("%s optional CFI branch not present at %p", name, target);
  return 0;
}

static int patch_nop_short_cfi_branch(uint8_t* target, const char* name) {
  if (target[0] != 0x77) {
    probe_log("%s short CFI branch mismatch at %p", name, target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect %s short CFI branch failed: %s", name,
              strerror(errno));
    return 0;
  }
  target[0] = 0x90;
  target[1] = 0x90;
  __builtin___clear_cache((char*)target, (char*)target + 2);
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched %s short CFI branch target=%p", name, target);
  return 1;
}

static int patch_cfi_slowpath_call_success(uint8_t* target, const char* name) {
  if (target[0] != 0xE8) {
    probe_log("%s CFI slowpath call mismatch at %p", name, target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect %s CFI slowpath call failed: %s", name,
              strerror(errno));
    return 0;
  }
  // The following Chrome code does "xor $1, %al; or %bl, %al; je trap".
  // Returning AL=0 from the skipped slowpath makes the post-check non-zero.
  uint8_t patch[5] = {0x31, 0xC0, 0x90, 0x90, 0x90};  // xor eax,eax; nops
  memcpy(target, patch, sizeof(patch));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(patch));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched %s CFI slowpath call target=%p", name, target);
  return 1;
}

static int patch_nop_ud2(uint8_t* target, const char* name) {
  uint8_t expected[2] = {0x0F, 0x0B};
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("%s ud2 mismatch at %p", name, target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect %s ud2 failed: %s", name, strerror(errno));
    return 0;
  }
  target[0] = 0x90;
  target[1] = 0x90;
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched %s ud2 target=%p", name, target);
  return 1;
}

typedef void* (*ConstructH265DelegateFn)(void* storage,
                                         void* vaapi_wrapper,
                                         void* error_cb);
typedef void* (*CreateH265DelegateFromAbiFn)(void* vaapi_wrapper,
                                             void* error_cb);
typedef void (*H264DelegateCtorFn)(void* self,
                                   void* vaapi_wrapper,
                                   void* error_cb);

__attribute__((used, noinline)) static void* h264_delegate_ctor_hook(
    void* self,
    void* vaapi_wrapper,
    void* error_cb) {
  char entry_line[192];
  int entry_n = snprintf(entry_line, sizeof(entry_line),
                         "hit H264 delegate ctor hook self=%p wrapper=%p "
                         "error_cb=%p next_h265=%d\n",
                         self, vaapi_wrapper, error_cb,
                         g_next_delegate_should_be_h265);
  if (entry_n > 0) {
    ssize_t ignored =
        write(g_log_fd, entry_line,
              (size_t)(entry_n < (int)sizeof(entry_line)
                           ? entry_n
                           : (int)sizeof(entry_line)));
    (void)ignored;
  }

  if (g_next_delegate_should_be_h265) {
    g_next_delegate_should_be_h265 = 0;
    CreateH265DelegateFromAbiFn create_h265 =
        (CreateH265DelegateFromAbiFn)dlsym(
            RTLD_DEFAULT, "chrome_hevc_create_h265_delegate_from_abi");
    if (create_h265) {
      void* out = create_h265(vaapi_wrapper, error_cb);
      char line[192];
      int n = snprintf(line, sizeof(line),
                       "created H265 delegate from H264 ctor storage=%p "
                       "wrapper=%p out=%p\n",
                       self, vaapi_wrapper, out);
      if (n > 0) {
        ssize_t ignored =
            write(g_log_fd, line,
                  (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line)));
        (void)ignored;
      }
      if (out) {
        install_crash_logger();
        return out;
      }
    } else {
      static const char kMissing[] =
          "chrome_hevc_create_h265_delegate_from_abi not found; "
          "using H264 ctor\n";
      probe_log_fast(kMissing, sizeof(kMissing) - 1);
    }
  }

  H264DelegateCtorFn original = (H264DelegateCtorFn)g_h264_delegate_ctor_trampoline;
  original(self, vaapi_wrapper, error_cb);
  return self;
}

static int install_h264_delegate_ctor_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_H264_DELEGATE_CTOR] = {
      0x55,                          // push rbp
      0x48, 0x89, 0xE5,              // mov rsp, rbp
      0x41, 0x57,                    // push r15
      0x41, 0x56,                    // push r14
      0x41, 0x54,                    // push r12
      0x53,                          // push rbx
      0x48, 0x83, 0xEC, 0x10,        // sub rsp, 0x10
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)h264_delegate_ctor_hook,
                               &g_h264_delegate_ctor_trampoline,
                               "H264 delegate constructor");
}

static int patch_h264_arm_store_encoder_from_rax(uint8_t* target) {
  uint8_t expected[7] = {
      0x4C, 0x89, 0xBB, 0x58, 0x01, 0x00, 0x00,  // mov r15, [rbx+0x158]
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("H264 arm encoder store mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect H264 arm encoder store failed: %s", strerror(errno));
    return 0;
  }
  uint8_t patch[7] = {
      0x48, 0x89, 0x83, 0x58, 0x01, 0x00, 0x00,  // mov rax, [rbx+0x158]
  };
  memcpy(target, patch, sizeof(patch));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(patch));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched H264 arm encoder store to use rax target=%p", target);
  return 1;
}

static int patch_adapter_disable_gpu_shared_images(uint8_t* target) {
  uint8_t expected[4] = {
      0x41, 0x8A, 0x45, 0x18,  // mov 0x18(%r13),%al
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("adapter supports-gpu-shared-images copy mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect adapter supports-gpu-shared-images failed: %s",
              strerror(errno));
    return 0;
  }

  uint8_t patch[4] = {
      0x31, 0xC0, 0x90, 0x90,  // xor eax,eax; nop; nop
  };
  memcpy(target, patch, sizeof(patch));
  __builtin___clear_cache((char*)target, (char*)target + sizeof(patch));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched adapter to disable GPU shared-image input target=%p",
            target);
  return 1;
}

static int patch_setup_vea_config_force_shmem(uint8_t* target) {
  uint8_t expected[8] = {
      0x41, 0xC7, 0x46, 0x20, 0x01, 0x00, 0x00, 0x00,
      // movl $0x1,0x20(%r14)
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("SetUpVeaConfig storage-type patch mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect SetUpVeaConfig storage-type failed: %s",
              strerror(errno));
    return 0;
  }

  target[4] = 0x00;  // media::VideoEncodeAccelerator::Config::StorageType::kShmem
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched SetUpVeaConfig storage-type to kShmem target=%p", target);
  return 1;
}

static int patch_setup_vea_config_force_i420(uint8_t* target) {
  uint8_t expected[5] = {
      0xBE, 0x06, 0x00, 0x00, 0x00,  // mov $0x6,%esi (PIXEL_FORMAT_NV12)
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("SetUpVeaConfig input-format patch mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect SetUpVeaConfig input-format failed: %s",
              strerror(errno));
    return 0;
  }

  target[1] = 0x01;  // media::PIXEL_FORMAT_I420
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched SetUpVeaConfig input-format to I420 target=%p", target);
  return 1;
}

static int patch_encode_force_prepare_cpu(uint8_t* target) {
  uint8_t expected[2] = {
      0x75, 0x3A,  // jne PrepareCpuFrame path
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("EncodeOnAcceleratorThread CPU-prepare branch mismatch at %p",
              target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect EncodeOnAcceleratorThread CPU branch failed: %s",
              strerror(errno));
    return 0;
  }

  target[0] = 0xEB;  // Always jump to the PrepareCpuFrame path.
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched EncodeOnAcceleratorThread to force PrepareCpuFrame "
            "target=%p",
            target);
  return 1;
}

static int patch_call_target(uint8_t* call_site,
                             uintptr_t call_site_vma,
                             uintptr_t old_target_vma,
                             uintptr_t new_target_vma,
                             const char* name) {
  if (call_site[0] != 0xE8) {
    probe_log("%s call opcode mismatch at %p", name, call_site);
    return 0;
  }
  int32_t old_rel = 0;
  memcpy(&old_rel, call_site + 1, sizeof(old_rel));
  uintptr_t current_target =
      call_site_vma + 5 + (intptr_t)old_rel;
  if (current_target != old_target_vma) {
    probe_log("%s call target mismatch at %p current=%p expected=%p", name,
              call_site, (void*)current_target, (void*)old_target_vma);
    return 0;
  }

  intptr_t rel = (intptr_t)new_target_vma - (intptr_t)(call_site_vma + 5);
  if (rel < INT32_MIN || rel > INT32_MAX) {
    probe_log("%s new call target out of range at %p", name, call_site);
    return 0;
  }
  int32_t new_rel = (int32_t)rel;

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)call_site & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect %s failed: %s", name, strerror(errno));
    return 0;
  }
  memcpy(call_site + 1, &new_rel, sizeof(new_rel));
  __builtin___clear_cache((char*)call_site, (char*)call_site + 5);
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched %s call target=%p -> %p site=%p", name,
            (void*)old_target_vma, (void*)new_target_vma, call_site);
  return 1;
}

static void patch_prepare_gpu_calls_to_cpu(uintptr_t base) {
  for (size_t i = 0; i < sizeof(kChrome146PrepareGpuFrameCallOffsets) /
                             sizeof(kChrome146PrepareGpuFrameCallOffsets[0]);
       ++i) {
    uintptr_t offset = kChrome146PrepareGpuFrameCallOffsets[i];
    patch_call_target((uint8_t*)(base + offset), offset + 0x1000,
                      kChrome146PrepareGpuFrameAddress,
                      kChrome146PrepareCpuFrameAddress,
                      "PrepareGpuFrame->PrepareCpuFrame");
  }
}

static int patch_prepare_gpu_entry_to_cpu(uint8_t* target, void* cpu_target) {
  uint8_t expected[PATCH_SIZE_SHORT] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("PrepareGpuFrame entry patch mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect PrepareGpuFrame entry failed: %s", strerror(errno));
    return 0;
  }
  memset(target, 0x90, sizeof(expected));
  write_abs_jump(target, cpu_target);
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched PrepareGpuFrame entry to PrepareCpuFrame target=%p cpu=%p",
            target, cpu_target);
  return 1;
}

static int patch_prepare_cpu_force_i420(uint8_t* fast_path_cmp,
                                        uint8_t* allocation_format,
                                        uint8_t* wrap_format) {
  uint8_t expected_fast_path[5] = {
      0x83, 0xFA, 0x06,  // cmp $0x6,%edx (PIXEL_FORMAT_NV12)
      0x74, 0x09,        // je fast-path accept
  };
  uint8_t expected_format[5] = {
      0xBF, 0x06, 0x00, 0x00, 0x00,  // mov $0x6,%edi
  };
  if (memcmp(fast_path_cmp, expected_fast_path, sizeof(expected_fast_path)) !=
      0) {
    probe_log("PrepareCpuFrame NV12 fast-path patch mismatch at %p",
              fast_path_cmp);
    return 0;
  }
  if (memcmp(allocation_format, expected_format, sizeof(expected_format)) !=
      0) {
    probe_log("PrepareCpuFrame allocation-format patch mismatch at %p",
              allocation_format);
    return 0;
  }
  if (memcmp(wrap_format, expected_format, sizeof(expected_format)) != 0) {
    probe_log("PrepareCpuFrame wrap-format patch mismatch at %p", wrap_format);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t first_page = (uintptr_t)fast_path_cmp & ~(uintptr_t)(page_size - 1);
  uintptr_t last_page = (uintptr_t)wrap_format & ~(uintptr_t)(page_size - 1);
  for (uintptr_t page = first_page; page <= last_page; page += page_size) {
    if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
        0) {
      probe_log("mprotect PrepareCpuFrame I420 failed: %s", strerror(errno));
      return 0;
    }
  }

  fast_path_cmp[2] = 0xFF;     // Do not accept NV12 as already-prepared CPU input.
  allocation_format[1] = 0x01; // media::PIXEL_FORMAT_I420
  wrap_format[1] = 0x01;       // media::PIXEL_FORMAT_I420
  __builtin___clear_cache((char*)fast_path_cmp, (char*)wrap_format + 5);

  for (uintptr_t page = first_page; page <= last_page; page += page_size) {
    mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  }
  probe_log("patched PrepareCpuFrame to materialize I420 fast=%p alloc=%p wrap=%p",
            fast_path_cmp, allocation_format, wrap_format);
  return 1;
}

static int patch_prepare_cpu_use_visible_size(uint8_t* target) {
  uint8_t expected[7] = {
      0x49, 0x8B, 0x85, 0xB0, 0x01, 0x00, 0x00,
      // mov 0x1b0(%r13),%rax
  };
  if (memcmp(target, expected, sizeof(expected)) != 0) {
    probe_log("PrepareCpuFrame coded-size patch mismatch at %p", target);
    return 0;
  }

  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  uintptr_t page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);
  if (mprotect((void*)page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) !=
      0) {
    probe_log("mprotect PrepareCpuFrame coded-size failed: %s",
              strerror(errno));
    return 0;
  }

  target[3] = 0x68;  // mov 0x168(%r13),%rax (options_.frame_size)
  __builtin___clear_cache((char*)target, (char*)target + sizeof(expected));
  mprotect((void*)page, page_size, PROT_READ | PROT_EXEC);
  probe_log("patched PrepareCpuFrame coded-size to visible size target=%p",
            target);
  return 1;
}

static int install_ctor_hook(uint8_t* target) {
  uint8_t expected[PATCH_SIZE_SHORT] = {
      0x55,                         // push rbp
      0x48, 0x89, 0xE5,             // mov rsp, rbp
      0x41, 0x57,                   // push r15
      0x41, 0x56,                   // push r14
      0x41, 0x55,                   // push r13
      0x41, 0x54,                   // push r12
      0x53,                         // push rbx
  };
  return install_abs_jump_hook(target, expected, sizeof(expected),
                               (void*)vea_ctor_hook,
                               &g_ctor_trampoline, "VEA constructor");
}

__attribute__((constructor)) static void init(void) {
  probe_log("loaded");

  const char* enabled = getenv("CHROME_HEVC_TRAMPOLINE_PROBE");
  if (!enabled || strcmp(enabled, "1") != 0) {
    probe_log("disabled; set CHROME_HEVC_TRAMPOLINE_PROBE=1 to patch");
    return;
  }
  uintptr_t base = 0;
  if (!find_chrome_text_base(&base)) {
    return;
  }
  probe_log("chrome_base=%p", (void*)base);

  const char* force_cpu_input =
      getenv("CHROME_HEVC_TRAMPOLINE_FORCE_CPU_INPUT");
  const char* force_cpu_input_browser =
      getenv("CHROME_HEVC_TRAMPOLINE_FORCE_CPU_INPUT_BROWSER");
  if (force_cpu_input && strcmp(force_cpu_input, "1") == 0 &&
      (strcmp(process_type(), "browser") != 0 ||
       (force_cpu_input_browser && strcmp(force_cpu_input_browser, "1") == 0))) {
    if (env_flag_enabled_or_default(
            "CHROME_HEVC_TRAMPOLINE_HOOK_MAPPABLE_CREATE", 0)) {
      install_mappable_maybe_create_hook(
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_MAPPABLE_MAYBE_CREATE",
                                kChrome146MappableMaybeCreateOffset)));
    }
    if (env_flag_enabled_or_default(
            "CHROME_HEVC_TRAMPOLINE_HOOK_READONLY_POOL", 0)) {
      install_readonly_pool_maybe_allocate_hook(
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_READONLY_POOL_MAYBE_ALLOCATE",
                                kChrome146ReadOnlyRegionPoolMaybeAllocateOffset)));
    }
    if (env_flag_enabled_or_default(
            "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_PATCH_PROFILE", 1)) {
      patch_adapter_disable_gpu_shared_images(
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_ADAPTER_COPY_SUPPORTS_GPU_SHARED_IMAGES",
                                kChrome146AdapterCopySupportsGpuSharedImagesOffset)));
    }
    if (env_flag_enabled_or_default(
            "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_PATCH_SHMEM", 1)) {
      patch_setup_vea_config_force_shmem(
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_SETUP_VEA_CONFIG_STORAGE_TYPE",
                                kChrome146SetUpVeaConfigStorageTypeOffset)));
    }
    if (env_flag_enabled_or_default(
            "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_PATCH_I420", 1)) {
      patch_setup_vea_config_force_i420(
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_SETUP_VEA_CONFIG_INPUT_FORMAT",
                                kChrome146SetUpVeaConfigInputFormatNv12Offset)));
    }
    if (env_flag_enabled_or_default(
            "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_PATCH_PREPARE", 1)) {
      patch_prepare_gpu_entry_to_cpu(
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_PREPARE_GPU_FRAME",
                                kChrome146PrepareGpuFrameOffset)),
          (void*)(base + env_offset_or_default(
                            "CHROME_HEVC_OFF_PREPARE_CPU_FRAME",
                            kChrome146PrepareCpuFrameOffset)));
      if (env_flag_enabled_or_default(
              "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_PATCH_PREPARE_I420", 1)) {
        patch_prepare_cpu_force_i420(
            (uint8_t*)(base + env_offset_or_default(
                                  "CHROME_HEVC_OFF_PREPARE_CPU_FRAME_NV12_FAST_PATH_CMP",
                                  kChrome146PrepareCpuFrameNv12FastPathCmpOffset)),
            (uint8_t*)(base + env_offset_or_default(
                                  "CHROME_HEVC_OFF_PREPARE_CPU_FRAME_ALLOCATION_FORMAT",
                                  kChrome146PrepareCpuFrameAllocationFormatOffset)),
            (uint8_t*)(base + env_offset_or_default(
                                  "CHROME_HEVC_OFF_PREPARE_CPU_FRAME_WRAP_FORMAT",
                                  kChrome146PrepareCpuFrameWrapFormatOffset)));
      }
      if (env_flag_enabled_or_default(
              "CHROME_HEVC_TRAMPOLINE_FORCE_CPU_PATCH_VISIBLE_SIZE", 1)) {
        patch_prepare_cpu_use_visible_size(
            (uint8_t*)(base +
                       env_offset_or_default(
                           "CHROME_HEVC_OFF_PREPARE_CPU_FRAME_DEST_CODED_SIZE_LOAD",
                           kChrome146PrepareCpuFrameDestCodedSizeLoadOffset)));
      }
      if (env_flag_enabled_or_default(
              "CHROME_HEVC_TRAMPOLINE_HOOK_PREPARE_CPU", 0)) {
        install_prepare_cpu_frame_hook(
            (uint8_t*)(base + env_offset_or_default(
                                  "CHROME_HEVC_OFF_PREPARE_CPU_FRAME",
                                  kChrome146PrepareCpuFrameOffset)));
      }
      if (env_flag_enabled_or_default(
              "CHROME_HEVC_TRAMPOLINE_HOOK_REQUIRE_BITSTREAM", 0)) {
        install_require_bitstream_buffers_hook(
            (uint8_t*)(base + env_offset_or_default(
                                  "CHROME_HEVC_OFF_REQUIRE_BITSTREAM_BUFFERS",
                                  kChrome146RequireBitstreamBuffersOffset)));
      }
    }
  }

  if (!can_install_patch_in_this_process()) {
    probe_log("not zygote/gpu-process; skip VEA/VAAPI patch");
    return;
  }
  install_crash_logger();

  uint8_t* target = (uint8_t*)(base + env_offset_or_default(
                                          "CHROME_HEVC_OFF_VEA_INITIALIZE",
                                          kChrome146VeaInitializeOffset));
  probe_log("target=%p", target);
  uint8_t* ctor_target = (uint8_t*)(base + env_offset_or_default(
                                               "CHROME_HEVC_OFF_VEA_CTOR",
                                               kChrome146VeaCtorOffset));
  install_ctor_hook(ctor_target);
  install_hook(target);
  uint8_t* task_target = (uint8_t*)(base + env_offset_or_default(
                                               "CHROME_HEVC_OFF_VEA_INITIALIZE_TASK",
                                               kChrome146VeaInitializeTaskOffset));
  install_initialize_task_hook(task_target);
  const char* patch_init_mask =
      getenv("CHROME_HEVC_TRAMPOLINE_PATCH_INITIALIZE_CODEC_MASK");
  if (patch_init_mask && strcmp(patch_init_mask, "1") == 0) {
    uint8_t* init_mask_target =
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_VEA_ACCEPTED_CODEC_MASK",
                              kChrome146VeaAcceptedCodecMaskOffset));
    patch_accept_hevc_in_initialize_codec_mask(init_mask_target);
  }
  const char* patch_vbr =
      getenv("CHROME_HEVC_TRAMPOLINE_BYPASS_INITIALIZE_VBR_RESTRICTION");
  if (patch_vbr && strcmp(patch_vbr, "1") == 0) {
    uint8_t* vbr_target =
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_VEA_BYPASS_VBR_RESTRICTION",
                              kChrome146VeaBypassVbrRestrictionOffset));
    patch_bypass_initialize_vbr_restriction(vbr_target);
  }
  const char* patch_mode = getenv("CHROME_HEVC_TRAMPOLINE_PATCH_MODE_SWITCH");
  if (patch_mode && strcmp(patch_mode, "1") == 0) {
    uint8_t* mode_switch_target =
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_VEA_ACCEPT_VP8_VP9_RANGE_CMP",
                              kChrome146VeaAcceptVp8Vp9RangeCmpOffset));
    patch_accept_hevc_in_mode_selection(mode_switch_target);
  }
  const char* force_mode_accept =
      getenv("CHROME_HEVC_TRAMPOLINE_FORCE_MODE_ACCEPT");
  if (force_mode_accept && strcmp(force_mode_accept, "1") == 0) {
    uint8_t* force_mode_target =
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_VEA_FORCE_MODE_ACCEPT",
                              kChrome146VeaForceModeAcceptOffset));
    patch_force_mode_selection_accept(force_mode_target);
  }
  const char* force_cbr = getenv("CHROME_HEVC_TRAMPOLINE_FORCE_CBR_MODE");
  if (force_cbr && strcmp(force_cbr, "1") == 0) {
    uint8_t* default_mode_target =
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_VEA_DEFAULT_ENCODE_MODE",
                              kChrome146VeaDefaultEncodeModeOffset));
    patch_default_encode_mode(default_mode_target, 1);
  }
  const char* patch_delegate_switch =
      getenv("CHROME_HEVC_TRAMPOLINE_PATCH_DELEGATE_SWITCH_TO_H264");
  if (patch_delegate_switch && strcmp(patch_delegate_switch, "1") == 0) {
    uintptr_t delegate_table_offset = env_offset_or_default(
        "CHROME_HEVC_OFF_VEA_DELEGATE_SWITCH_TABLE", 0);
    if (delegate_table_offset != 0) {
      patch_delegate_switch_hevc_to_h264_from_table(
          (uint8_t*)(base + delegate_table_offset));
    } else {
      uint8_t* delegate_switch_target =
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_VEA_DELEGATE_SWITCH_HEVC_ENTRY",
                                kChrome146VeaDelegateSwitchHevcEntryOffset));
      patch_delegate_switch_hevc_to_h264(delegate_switch_target);
    }
  }
  const char* replace_delegate =
      getenv("CHROME_HEVC_TRAMPOLINE_REPLACE_H264_DELEGATE_WITH_H265");
  if (replace_delegate && strcmp(replace_delegate, "1") == 0) {
    uint8_t* h264_delegate_ctor_target =
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_H264_DELEGATE_CTOR",
                              kChrome146H264DelegateCtorOffset));
    install_h264_delegate_ctor_hook(h264_delegate_ctor_target);
    patch_h264_arm_store_encoder_from_rax(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_H264_ARM_STORE_ENCODER",
                              kChrome146H264ArmStoreEncoderOffset)));
    patch_nop_cfi_branch((uint8_t*)(base + env_offset_or_default(
                                              "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_A",
                                              kChrome146DelegateCfiCheckAOffset)),
                         "delegate CFI A");
    patch_nop_cfi_branch((uint8_t*)(base + env_offset_or_default(
                                              "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_B",
                                              kChrome146DelegateCfiCheckBOffset)),
                         "delegate CFI B");
    patch_nop_cfi_branch((uint8_t*)(base + env_offset_or_default(
                                              "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_C",
                                              kChrome146DelegateCfiCheckCOffset)),
                         "delegate CFI C");
    patch_nop_cfi_branch((uint8_t*)(base + env_offset_or_default(
                                              "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_D",
                                              kChrome146DelegateCfiCheckDOffset)),
                         "delegate CFI D");
    patch_nop_cfi_branch((uint8_t*)(base + env_offset_or_default(
                                              "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_INIT",
                                              kChrome146DelegateCfiCheckInitOffset)),
                         "delegate Initialize");
    uintptr_t init_extra_offset = env_offset_or_default(
        "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_INIT_EXTRA", 0);
    if (init_extra_offset != 0) {
      patch_nop_cfi_branch((uint8_t*)(base + init_extra_offset),
                           "delegate Initialize extra");
    }
    patch_cfi_slowpath_call_success(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_INITIALIZE_CFI_SLOWPATH_CALL",
                              kChrome146DelegateInitializeCfiSlowpathCallOffset)),
        "delegate Initialize");
    patch_cfi_slowpath_call_success(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_INITIALIZE_CFI_RUNTIME_CALL",
                              kChrome146DelegateInitializeCfiRuntimeCallOffset)),
        "delegate Initialize runtime");
    patch_nop_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_GET_FRAMES",
                              kChrome146DelegateCfiCheckGetFramesOffset)),
        "delegate GetMaxNumOfRefFrames");
    patch_nop_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_CFI_CHECK_GET_BITSTREAM",
                              kChrome146DelegateCfiCheckGetBitstreamOffset)),
        "delegate GetBitstreamBufferMetadata");
    patch_nop_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_ENCODE_CFI_CHECK",
                              kChrome146DelegateEncodeCfiCheckOffset)),
        "delegate encode");
    patch_nop_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_ENCODE_JOB_CFI_CHECK",
                              kChrome146DelegateEncodeJobCfiCheckOffset)),
        "delegate encode job");
    patch_nop_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK",
                              kChrome146DelegatePrepareEncodeJobCfiCheckOffset)),
        "delegate PrepareEncodeJob");
    uintptr_t prepare_extra_offset = env_offset_or_default(
        "CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK_EXTRA", 0);
    if (prepare_extra_offset != 0) {
      patch_nop_cfi_branch_optional((uint8_t*)(base + prepare_extra_offset),
                                    "delegate PrepareEncodeJob extra");
    }
    uintptr_t prepare_extra2_offset = env_offset_or_default(
        "CHROME_HEVC_OFF_DELEGATE_PREPARE_ENCODE_JOB_CFI_CHECK_EXTRA2", 0);
    if (prepare_extra2_offset != 0) {
      patch_nop_cfi_branch_optional((uint8_t*)(base + prepare_extra2_offset),
                                    "delegate PrepareEncodeJob extra2");
    }
    patch_nop_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_GET_METADATA_CFI_CHECK",
                              kChrome146DelegateGetMetadataCfiCheckOffset)),
        "delegate GetBitstreamBufferMetadata result");
    patch_nop_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_METADATA_CLEANUP_CFI_CHECK",
                              kChrome146DelegateMetadataCleanupCfiCheckOffset)),
        "delegate GetBitstreamBufferMetadata cleanup");
    patch_nop_short_cfi_branch(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_DELEGATE_CLEANUP_SHORT_CFI_BRANCH",
                              kChrome146DelegateCleanupShortCfiBranchOffset)),
        "delegate cleanup");
    patch_nop_ud2((uint8_t*)(base + env_offset_or_default(
                                        "CHROME_HEVC_OFF_INITIALIZE_TASK_POST_DELEGATE_UD2",
                                        kChrome146InitializeTaskPostDelegateUd2Offset)),
                  "InitializeTask post-delegate");
  }
  if (env_flag_enabled_or_default(
          "CHROME_HEVC_TRAMPOLINE_HOOK_CREATE_ENCODE_JOB", 0)) {
    install_create_encode_job_hook(
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_CREATE_ENCODE_JOB",
                              kChrome146CreateEncodeJobOffset)));
  }
  uint8_t* profiles_target =
      (uint8_t*)(base + env_offset_or_default(
                            "CHROME_HEVC_OFF_VEA_GET_SUPPORTED_PROFILES",
                            kChrome146VeaGetSupportedProfilesOffset));
  const char* hook_profiles = getenv("CHROME_HEVC_TRAMPOLINE_HOOK_PROFILES");
  if (hook_profiles && strcmp(hook_profiles, "1") == 0) {
    install_get_supported_profiles_hook(profiles_target);
  } else {
    probe_log("VEA GetSupportedProfiles hook disabled; set "
              "CHROME_HEVC_TRAMPOLINE_HOOK_PROFILES=1 to test it");
  }

  uint8_t* create_target =
      (uint8_t*)(base + env_offset_or_default(
                            "CHROME_HEVC_OFF_VAAPI_CREATE_FOR_VIDEO_CODEC",
                            kChrome146VaapiCreateForVideoCodecOffset));
  const char* hook_create = getenv("CHROME_HEVC_TRAMPOLINE_HOOK_CREATE");
  if (hook_create && strcmp(hook_create, "1") == 0) {
    install_create_for_video_codec_hook(create_target);
    uint8_t* vaapi_create_target =
        (uint8_t*)(base + env_offset_or_default(
                              "CHROME_HEVC_OFF_VAAPI_CREATE",
                              kChrome146VaapiCreateOffset));
    install_vaapi_create_hook(vaapi_create_target);
    const char* bypass_profile_table =
        getenv("CHROME_HEVC_TRAMPOLINE_BYPASS_VAAPI_PROFILE_TABLE_MISS");
    if (bypass_profile_table && strcmp(bypass_profile_table, "1") == 0) {
      uint8_t* table_miss_target =
          (uint8_t*)(base + env_offset_or_default(
                                "CHROME_HEVC_OFF_VAAPI_CREATE_BYPASS_PROFILE_TABLE_MISS",
                                kChrome146VaapiCreateBypassProfileTableMissOffset));
      patch_bypass_vaapi_profile_table_miss(table_miss_target);
    }
  } else {
    probe_log("VaapiWrapper CreateForVideoCodec hook disabled; set "
              "CHROME_HEVC_TRAMPOLINE_HOOK_CREATE=1 to test it");
  }
}
