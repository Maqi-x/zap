#include "runtime/arc_layout.h"

#include <stddef.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct test_object_t {
  zap_arc_header_t header;
  void *child;
} test_object_t;

static int destroy_count = 0;
static int preserved_strong_counts = 1;
static int schedule_reentrant_collection = 0;
static zap_arc_runtime_context_t *runtime_context = NULL;
static test_object_t *reentrant_first = NULL;
static test_object_t *reentrant_second = NULL;

static void test_destroy(void *object) {
  test_object_t *test_object = (test_object_t *)object;
  if (test_object->header.strong_count != 1) {
    preserved_strong_counts = 0;
  }
  test_object->header.alive = 0;
  ++destroy_count;
  if (schedule_reentrant_collection) {
    schedule_reentrant_collection = 0;
    zap_arc_add_possible_root(runtime_context, reentrant_first);
    zap_arc_add_possible_root(runtime_context, reentrant_second);
    zap_arc_collect_at_safepoint(runtime_context);
  }
}

static void trace_child(void *object, zap_arc_trace_visitor_t visitor,
                        void *context) {
  test_object_t *test_object = (test_object_t *)object;
  visitor(context, test_object->child);
}

static int expect(int condition, const char *message) {
  if (!condition) {
    fputs(message, stderr);
    fputc('\n', stderr);
  }
  return condition;
}

static test_object_t *make_test_object(const zap_arc_metadata_t *metadata) {
  test_object_t *object = (test_object_t *)calloc(1, sizeof(test_object_t));
  if (!object) {
    return NULL;
  }
  object->header.strong_count = 1;
  object->header.alive = 1;
  object->header.destroy_fn = test_destroy;
  object->header.metadata = metadata;
  return object;
}

static int test_direct_events_and_allocation(void) {
  zap_runtime_ownership_reset_counters();
  void *allocation = zap_runtime_alloc(1);
  free(allocation);
  zap_runtime_ownership_note_strong_retain();
  zap_runtime_ownership_note_strong_release();
  zap_runtime_ownership_note_copy();
  zap_runtime_ownership_note_drop();
  zap_runtime_ownership_note_destroy();

  zap_runtime_ownership_counters_t counters = {0};
  zap_runtime_ownership_snapshot_counters(&counters);
  return expect(counters.allocations == 1, "allocation counter is incorrect") &&
         expect(counters.strong_retain_calls == 1,
                "strong retain counter is incorrect") &&
         expect(counters.strong_release_calls == 1,
                "strong release counter is incorrect") &&
         expect(counters.copy_operations == 1,
                "copy-operation counter is incorrect") &&
         expect(counters.drop_operations == 1,
                "drop-operation counter is incorrect") &&
         expect(counters.destroy_calls == 1, "destroy counter is incorrect");
}

static int test_cycle_collection_events(void) {
  static const zap_arc_metadata_t metadata = {trace_child};
  test_object_t *first = make_test_object(&metadata);
  test_object_t *second = make_test_object(&metadata);
  reentrant_first = make_test_object(&metadata);
  reentrant_second = make_test_object(&metadata);
  if (!expect(first != NULL && second != NULL && reentrant_first != NULL &&
                  reentrant_second != NULL,
              "failed to allocate cycle test objects")) {
    free(first);
    free(second);
    free(reentrant_first);
    free(reentrant_second);
    return 0;
  }
  first->child = second;
  second->child = first;
  reentrant_first->child = reentrant_second;
  reentrant_second->child = reentrant_first;
  first->header.weak_count = 1;
  zap_arc_runtime_context_t *context = zap_arc_default_context();

  destroy_count = 0;
  preserved_strong_counts = 1;
  schedule_reentrant_collection = 1;
  runtime_context = context;
  zap_runtime_ownership_reset_counters();
  zap_arc_add_possible_root(context, first);
  zap_arc_add_possible_root(context, second);

  zap_runtime_ownership_counters_t counters = {0};
  zap_runtime_ownership_snapshot_counters(&counters);
  if (!expect(counters.candidate_roots == 2,
              "candidate-root counter is incorrect") ||
      !expect(counters.collection_runs == 0,
              "collection ran before a safe point")) {
    return 0;
  }

  zap_arc_collect_at_safepoint(context);
  zap_runtime_ownership_snapshot_counters(&counters);
  int passed = expect(counters.collection_runs == 1,
                      "reentrant collection ran before the next safe point") &&
      expect(counters.visited_objects == 2,
                      "visited-object counter is incorrect") &&
      expect(counters.reclaimed_objects == 2,
                      "reclaimed-object counter is incorrect") &&
      expect(destroy_count == 2,
                      "initial cycle objects were not destroyed exactly once") &&
      expect(counters.candidate_roots == 4,
                      "destructor did not schedule follow-up collection") &&
               expect(preserved_strong_counts,
                      "collector rewrote a cycle object's strong count") &&
               expect(first->header.alive == 0,
                      "weakly referenced cycle object is still alive") &&
               expect(first->header.strong_count == 1,
                      "weak tombstone did not preserve its strong count");
  zap_arc_collect_at_safepoint(context);
  zap_runtime_ownership_snapshot_counters(&counters);
  passed = passed &&
           expect(counters.collection_runs == 2,
                  "scheduled follow-up collection did not run") &&
           expect(counters.reclaimed_objects == 4,
                  "follow-up cycle was not reclaimed") &&
           expect(destroy_count == 4,
                  "follow-up cycle destructors did not run exactly once");
  first->header.weak_count = 0;
  zap_arc_deallocate(context, first);
  return passed;
}

static int test_retain_dead_object_fails(void) {
  pid_t child = fork();
  if (child < 0) {
    return expect(0, "failed to fork retain-dead-object test");
  }
  if (child == 0) {
    zap_arc_retain_dead_object();
    _exit(0);
  }

  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    return expect(0, "failed to wait for retain-dead-object test");
  }
  return expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
                "retaining a dead object did not abort");
}

int main(void) {
  return test_direct_events_and_allocation() && test_cycle_collection_events() &&
                 test_retain_dead_object_fails()
             ? 0
             : 1;
}
