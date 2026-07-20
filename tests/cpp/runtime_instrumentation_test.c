#include "runtime/arc_layout.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct test_object_t {
  zap_arc_header_t header;
  void *child;
} test_object_t;

static int destroy_count = 0;

static void test_destroy(void *object) {
  test_object_t *test_object = (test_object_t *)object;
  test_object->header.alive = 0;
  ++destroy_count;
}

static void test_release(void *object) {
  test_object_t *test_object = (test_object_t *)object;
  --test_object->header.strong_count;
  if (test_object->header.strong_count == 0) {
    test_object->header.destroy_fn(object);
  }
}

static int expect(int condition, const char *message) {
  if (!condition) {
    fputs(message, stderr);
    fputc('\n', stderr);
  }
  return condition;
}

static test_object_t make_test_object(const zap_arc_metadata_t *metadata) {
  test_object_t object = {0};
  object.header.strong_count = 1;
  object.header.alive = 1;
  object.header.release_fn = test_release;
  object.header.destroy_fn = test_destroy;
  object.header.metadata = metadata;
  return object;
}

static int test_direct_events_and_allocation(void) {
  zap_runtime_ownership_reset_counters();
  void *allocation = zap_runtime_alloc(1);
  free(allocation);
  zap_runtime_ownership_note_strong_retain();
  zap_runtime_ownership_note_strong_release();
  zap_runtime_ownership_note_destroy();

  zap_runtime_ownership_counters_t counters = {0};
  zap_runtime_ownership_snapshot_counters(&counters);
  return expect(counters.allocations == 1, "allocation counter is incorrect") &&
         expect(counters.strong_retain_calls == 1,
                "strong retain counter is incorrect") &&
         expect(counters.strong_release_calls == 1,
                "strong release counter is incorrect") &&
         expect(counters.destroy_calls == 1, "destroy counter is incorrect");
}

static int test_cycle_collection_events(void) {
  static const uint32_t child_offset = offsetof(test_object_t, child);
  static const zap_arc_metadata_t metadata = {1, &child_offset};
  test_object_t first = make_test_object(&metadata);
  test_object_t second = make_test_object(&metadata);
  first.child = &second;
  second.child = &first;

  destroy_count = 0;
  zap_runtime_ownership_reset_counters();
  zap_arc_add_possible_root(&first);
  zap_arc_add_possible_root(&second);
  zap_arc_cycle_collect();

  zap_runtime_ownership_counters_t counters = {0};
  zap_runtime_ownership_snapshot_counters(&counters);
  return expect(counters.candidate_roots == 2,
                "candidate-root counter is incorrect") &&
         expect(counters.collection_runs == 1,
                "collection-run counter is incorrect") &&
         expect(counters.visited_objects == 2,
                "visited-object counter is incorrect") &&
         expect(counters.reclaimed_objects == 2,
                "reclaimed-object counter is incorrect") &&
         expect(destroy_count == 2, "cycle objects were not destroyed");
}

int main(void) {
  return test_direct_events_and_allocation() && test_cycle_collection_events()
             ? 0
             : 1;
}
