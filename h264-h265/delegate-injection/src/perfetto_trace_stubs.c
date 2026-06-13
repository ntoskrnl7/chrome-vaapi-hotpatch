// Minimal stubs for Perfetto trace-only code paths pulled in by the delegate
// object.  The hotpatch does not rely on Chromium trace emission.

#define DEFINE_FUNC(local_name, symbol_name) \
  void* local_name(void) __asm__(symbol_name); \
  __attribute__((visibility("default"))) void* local_name(void) { return 0; }

#define DEFINE_VAR(local_name, symbol_name) \
  __attribute__((visibility("default"))) void* local_name[8] \
      __asm__(symbol_name) = {0}

DEFINE_FUNC(perfetto_log_message,
            "_ZN8perfetto4base10LogMessageENS0_6LogLevEPKciS3_z");
DEFINE_FUNC(perfetto_string_splitter_next,
            "_ZN8perfetto4base14StringSplitter4NextEv");
DEFINE_FUNC(perfetto_string_splitter_ctor,
            "_ZN8perfetto4base14StringSplitterC1ENSt4__Cr12basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEEcNS1_14EmptyTokenModeE");
DEFINE_FUNC(perfetto_read_file,
            "_ZN8perfetto4base8ReadFileERKNSt4__Cr12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEPS7_");
DEFINE_FUNC(perfetto_track_descriptor_ctor,
            "_ZN8perfetto6protos3gen15TrackDescriptorC1Ev");
DEFINE_FUNC(perfetto_track_descriptor_dtor,
            "_ZN8perfetto6protos3gen15TrackDescriptorD1Ev");
DEFINE_FUNC(perfetto_interned_event_name_add,
            "_ZN8perfetto8internal17InternedEventName3AddEPNS_6protos6pbzero12InternedDataEmPKc");
DEFINE_FUNC(perfetto_interned_event_category_add,
            "_ZN8perfetto8internal21InternedEventCategory3AddEPNS_6protos6pbzero12InternedDataEmPKcm");
DEFINE_FUNC(protozero_message_arena_new_message,
            "_ZN9protozero12MessageArena10NewMessageEv");
DEFINE_FUNC(protozero_message_arena_delete_last,
            "_ZN9protozero12MessageArena25DeleteLastMessageInternalEv");
DEFINE_FUNC(protozero_scattered_heap_buffer_reset,
            "_ZN9protozero19ScatteredHeapBuffer5ResetEv");
DEFINE_FUNC(protozero_scattered_heap_buffer_get_ranges,
            "_ZN9protozero19ScatteredHeapBuffer9GetRangesEv");
DEFINE_FUNC(perfetto_track_descriptor_serialize,
            "_ZNK8perfetto6protos3gen15TrackDescriptor17SerializeAsStringEv");

DEFINE_VAR(perfetto_platform_process_id, "_ZN8perfetto8Platform11process_id_E");
DEFINE_VAR(perfetto_tracing_muxer_fake_instance,
           "_ZN8perfetto8internal16TracingMuxerFake8instanceE");
DEFINE_VAR(perfetto_interned_event_name_vtable,
           "_ZTVN8perfetto8internal17InternedEventNameE");
DEFINE_VAR(perfetto_interned_event_category_vtable,
           "_ZTVN8perfetto8internal21InternedEventCategoryE");
