#define dlsym scout_test_dlsym
#include "../src/injector_scout.c"
#include <assert.h>

static unsigned int capabilities;
static VAStatus query_status;
static VAConfigAttrib received[32];
static int received_count;
static int query_count;

static VAStatus get_attributes(VADisplay dpy, VAProfile profile,
                               VAEntrypoint entrypoint,
                               VAConfigAttrib* attrs, int count) {
  assert(dpy == (VADisplay)1);
  assert(profile == kVAProfileHEVCMain || profile == kVAProfileHEVCMain10);
  assert(entrypoint == kVAEntrypointEncSlice);
  assert(count == 1 && attrs[0].type == kVAConfigAttribEncPackedHeaders);
  attrs[0].value = capabilities;
  ++query_count;
  return query_status;
}

static VAStatus create_config(VADisplay dpy, VAProfile profile,
                              VAEntrypoint entrypoint,
                              VAConfigAttrib* attrs, int count,
                              VAConfigID* config) {
  (void)dpy;
  (void)profile;
  (void)entrypoint;
  assert(count >= 0 && count <= 32);
  received_count = count;
  if (count) memcpy(received, attrs, (size_t)count * sizeof(*attrs));
  *config = 42;
  return 7;  // Preserve the driver's result, including errors.
}

static void check(VAProfile profile, VAEntrypoint entrypoint,
                  VAConfigAttrib* attrs, int count, unsigned int caps,
                  int append, int queries) {
  capabilities = caps;
  query_count = 0;
  VAConfigID config = 0;
  VAConfigAttrib before[16];
  if (count) memcpy(before, attrs, (size_t)count * sizeof(*attrs));
  assert(vaCreateConfig((VADisplay)1, profile, entrypoint, attrs, count, &config) == 7);
  assert(config == 42 && received_count == count + append);
  assert(query_count == queries);
  if (count) {
    assert(memcmp(before, attrs, (size_t)count * sizeof(*attrs)) == 0);
    assert(memcmp(before, received, (size_t)count * sizeof(*attrs)) == 0);
  }
  if (append) {
    assert(received[count].type == kVAConfigAttribEncPackedHeaders);
    assert(received[count].value == (caps & 7));
  }
}

int main(void) {
  g_real_vaCreateConfig = create_config;
  g_real_vaGetConfigAttributes = get_attributes;
  setenv("CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS", "1", 1);
  VAConfigAttrib attrs[16];
  for (int i = 0; i < 16; ++i) attrs[i] = (VAConfigAttrib){100 + i, (unsigned)i};
  check(17, 6, attrs, 2, 0xb, 1, 1);  // NVENC: no packed slice support.
  check(18, 6, attrs, 2, 0xb, 1, 1);
  check(17, 6, attrs, 2, 0x7, 1, 1);  // Intel: keep slice headers.
  check(17, 6, attrs, 16, 0xb, 1, 1);
  check(17, 6, NULL, 0, 0xb, 1, 1);
  check(17, 6, attrs, 2, 0, 0, 1);
  check(17, 6, attrs, 2, 0x80000000U, 0, 1);
  query_status = -1;
  check(17, 6, attrs, 2, 0xb, 0, 1);
  query_status = 0;
  check(7, 6, attrs, 2, 0xb, 0, 0);  // H.264 is untouched.
  check(17, 1, attrs, 2, 0xb, 0, 0);  // Decode is untouched.
  attrs[1] = (VAConfigAttrib){kVAConfigAttribEncPackedHeaders, 0};
  check(17, 6, attrs, 2, 0xb, 0, 0);  // Explicit NONE is authoritative.
  attrs[1].value = 3;
  check(17, 6, attrs, 2, 0xb, 0, 0);
  setenv("CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS", "0", 1);
  check(17, 6, NULL, 0, 0xb, 0, 0);
  unsetenv("CHROME_HEVC_SCOUT_APPEND_HEVC_PACKED_HEADERS");
  check(17, 6, NULL, 0, 0xb, 0, 0);
  puts("PASS: packed-header negotiation (14 cases)");
  return 0;
}
