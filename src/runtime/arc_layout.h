#pragma once

#include <stddef.h>
#include <stdint.h>

// Shared ARC object header ABI used by runtime (C) and codegen (C++).
#define ZAP_ARC_ABI_VERSION 1
#define ZAP_ARC_STRONG_COUNT_INDEX 0
#define ZAP_ARC_WEAK_COUNT_INDEX 1
#define ZAP_ARC_ALIVE_INDEX 2
#define ZAP_ARC_GC_MARK_INDEX 3
#define ZAP_ARC_RELEASE_FN_INDEX 4
#define ZAP_ARC_DESTROY_FN_INDEX 5
#define ZAP_ARC_METADATA_INDEX 6
#define ZAP_ARC_VTABLE_INDEX 7
#define ZAP_ARC_FIELD_START_INDEX 8
#define ZAP_ARC_HEADER_FIELD_COUNT ZAP_ARC_FIELD_START_INDEX

// Flag bits packed into the gc_mark byte (index 3).
#define ZAP_ARC_GC_GARBAGE 0x1
#define ZAP_ARC_GC_BUFFERED 0x2

typedef struct zap_arc_metadata_t {
  uint32_t strong_field_count;
  const uint32_t *strong_field_offsets;
} zap_arc_metadata_t;

typedef struct zap_arc_header_t {
  int64_t strong_count;
  int64_t weak_count;
  uint8_t alive;
  uint8_t gc_mark;
  void (*release_fn)(void *);
  void (*destroy_fn)(void *);
  const zap_arc_metadata_t *metadata;
  void **vtable;
} zap_arc_header_t;

#if defined(__cplusplus)
#define ZAP_ARC_STATIC_ASSERT(condition, message) static_assert(condition, message)
extern "C" {
#else
#define ZAP_ARC_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

void zap_arc_add_possible_root(void *object);
void zap_arc_remove_possible_root(void *object);
void zap_arc_cycle_collect(void);
void *zap_runtime_alloc(size_t size);
void zap_arc_strong_refcount_overflow(void);
void zap_arc_weak_refcount_overflow(void);
void zap_arc_strong_refcount_underflow(void);
void zap_arc_weak_refcount_underflow(void);

#if defined(__cplusplus)
}
#endif

ZAP_ARC_STATIC_ASSERT(ZAP_ARC_STRONG_COUNT_INDEX == 0,
                      "ARC ABI: strong_count index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_WEAK_COUNT_INDEX == 1,
                      "ARC ABI: weak_count index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_ALIVE_INDEX == 2,
                      "ARC ABI: alive index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_GC_MARK_INDEX == 3,
                      "ARC ABI: gc_mark index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_RELEASE_FN_INDEX == 4,
                      "ARC ABI: release_fn index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_DESTROY_FN_INDEX == 5,
                      "ARC ABI: destroy_fn index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_METADATA_INDEX == 6,
                      "ARC ABI: metadata index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_VTABLE_INDEX == 7,
                      "ARC ABI: vtable index mismatch");
ZAP_ARC_STATIC_ASSERT(ZAP_ARC_HEADER_FIELD_COUNT == ZAP_ARC_FIELD_START_INDEX,
                      "ARC ABI: header field count mismatch");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, strong_count) == 0,
                      "ARC ABI: strong_count offset mismatch");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, weak_count) >
                          offsetof(zap_arc_header_t, strong_count),
                      "ARC ABI: weak_count must be after strong_count");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, alive) >
                          offsetof(zap_arc_header_t, weak_count),
                      "ARC ABI: alive must be after weak_count");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, gc_mark) >
                          offsetof(zap_arc_header_t, alive),
                      "ARC ABI: gc_mark must be after alive");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, release_fn) >
                          offsetof(zap_arc_header_t, gc_mark),
                      "ARC ABI: release_fn must be after gc_mark");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, destroy_fn) >
                          offsetof(zap_arc_header_t, release_fn),
                      "ARC ABI: destroy_fn must be after release_fn");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, metadata) >
                          offsetof(zap_arc_header_t, destroy_fn),
                      "ARC ABI: metadata must be after destroy_fn");
ZAP_ARC_STATIC_ASSERT(offsetof(zap_arc_header_t, vtable) >
                          offsetof(zap_arc_header_t, metadata),
                      "ARC ABI: vtable must be after metadata");

#undef ZAP_ARC_STATIC_ASSERT
