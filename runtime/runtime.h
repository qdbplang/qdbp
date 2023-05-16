// This file gets included first in every qdbp progrram
// FIXME: Track mallocs and frees of JUDY arrays

#ifndef QDBP_RUNTIME_H
#define QDBP_RUNTIME_H
#include "bump.h"
#include "mempool.h"
#include <Judy.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// config
// You could also add fsanitize=undefined
// Features
// Not all are mutually exclusive
static const bool REFCOUNT = true;
static const bool BUMP_ALLOCATOR = true;
static const bool BINARY_SEARCH_ENABLED = true;
static const size_t BINARY_SEARCH_THRESHOLD = 30;
static const bool REUSE_PROTO_REPLACE = true;
static const bool OBJ_FREELIST = true;
static const bool FIELD_FREELIST = true;
static const bool CAPTURE_FREELIST = true;
#define FREELIST_SIZE 1000

// Change the # of times `action` is called for a different # of freelists
// IMPORTANT: Keep NUM_FREELISTS and FORALL_FREELISTS synced
static const size_t NUM_FREELISTS = 20;
#define FORALL_FREELISTS(action)                                               \
  action(1) action(2) action(3) action(4) action(5) action(6) action(7)        \
      action(8) action(9) action(10) action(11) action(12) action(13)          \
          action(14) action(15) action(16) action(17) action(18) action(19)    \
              action(20)

// Dynamic checks
static const bool CHECK_MALLOC_FREE = true; // very slow
static const bool VERIFY_REFCOUNTS = true;
static const bool DYNAMIC_TYPECHECK = true;

/*
    ====================================================
    Types
    ====================================================
*/
// FIXME: All identifiers should start with __qdbp
// FIXME: Clean all the adding to the list up
// FIXME: Wrap all object accesses in, for example, get_prototype that does
// assertion
// Have make string, make int, etc fns
// FIXME: Check all accesses are asserted
typedef uint64_t label_t;
typedef uint64_t tag_t;
typedef int64_t refcount_t;
struct qdbp_object;
struct qdbp_method {
  struct qdbp_object **captures;
  size_t captures_size;
  void *code;
};
struct qdbp_field {
  struct qdbp_method method;
};

struct qdbp_prototype {
  Pvoid_t labels;
};

struct qdbp_variant {
  tag_t tag;
  struct qdbp_object *value;
};

union qdbp_object_data {
  struct qdbp_prototype prototype;
  int64_t i;
  double f;
  char *s;
  struct qdbp_variant variant;
};

enum qdbp_object_kind {
  QDBP_INT,
  QDBP_FLOAT,
  QDBP_STRING,
  QDBP_PROTOTYPE,
  QDBP_VARIANT
};

struct qdbp_object {
  refcount_t refcount;
  enum qdbp_object_kind kind;
  union qdbp_object_data data;
};
typedef struct qdbp_object *qdbp_object_ptr;
typedef struct qdbp_object **qdbp_object_arr;
typedef struct qdbp_variant *qdbp_variant_ptr;
typedef struct qdbp_prototype *qdbp_prototype_ptr;
typedef struct qdbp_method *qdbp_method_ptr;
typedef struct qdbp_field *qdbp_field_ptr;
/*
====================================================
Basic Utilities
====================================================
*/

void print_object(qdbp_object_ptr obj) {
  switch (obj->kind) {
  case QDBP_INT:
    printf("int %lld\n", obj->data.i);
    break;
  case QDBP_FLOAT:
    printf("float %f\n", obj->data.f);
    break;
  case QDBP_STRING:
    printf("str %s\n", obj->data.s);
    break;
  case QDBP_PROTOTYPE:
    printf("PROTO\n");
    break;
  case QDBP_VARIANT:
    printf("variant %lld\n", obj->data.variant.tag);
    break;
  }
}
#define assert_refcount(obj)                                                   \
  do {                                                                         \
    if (VERIFY_REFCOUNTS) {                                                    \
      assert((obj));                                                           \
      if ((obj)->refcount <= 0) {                                              \
        printf("refcount of %lld\n", (obj)->refcount);                         \
        print_object(obj);                                                     \
        assert(false);                                                         \
      };                                                                       \
    }                                                                          \
  } while (0);
#define assert_obj_kind(obj, k)                                                \
  do {                                                                         \
    assert_refcount(obj);                                                      \
    if (DYNAMIC_TYPECHECK) {                                                   \
      assert((obj)->kind == k);                                                \
    }                                                                          \
  } while (0);
void qdbp_memcpy(void *dest, const void *src, size_t size) {
  memcpy(dest, src, size);
}
// Keep track of mallocs and frees
struct malloc_list_node {
  void *ptr;
  struct malloc_list_node *next;
};
struct malloc_list_node *malloc_list = NULL;
bool malloc_list_contains(void *ptr) {
  struct malloc_list_node *node = malloc_list;
  while (node) {
    if (node->ptr == ptr) {
      return true;
    }
    node = node->next;
  }
  return false;
}
void add_to_malloc_list(void *ptr) {
  assert(!malloc_list_contains(ptr));
  struct malloc_list_node *new_node =
      (struct malloc_list_node *)malloc(sizeof(struct malloc_list_node));
  new_node->ptr = ptr;
  new_node->next = malloc_list;
  malloc_list = new_node;
}
void remove_from_malloc_list(void *ptr) {
  assert(malloc_list_contains(ptr));
  struct malloc_list_node *node = malloc_list;
  struct malloc_list_node *prev = NULL;
  while (node) {
    if (node->ptr == ptr) {
      if (prev) {
        prev->next = node->next;
      } else {
        malloc_list = node->next;
      }
      free(node);
      return;
    }
    prev = node;
    node = node->next;
  }
  assert(false);
}
void *qdbp_malloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  void *ptr;
  if (BUMP_ALLOCATOR) {
    ptr = bump_alloc(size);
  } else {
    ptr = malloc(size);
  }
  if (CHECK_MALLOC_FREE) {
    add_to_malloc_list(ptr);
  }
  assert(ptr);
  return ptr;
}
void qdbp_free(void *ptr) {
  if (CHECK_MALLOC_FREE) {
    remove_from_malloc_list(ptr);
  }
  if (!BUMP_ALLOCATOR) {
    free(ptr);
  }
}

#define MK_FREELIST(type, name)                                                \
  struct name##_t {                                                            \
    type objects[FREELIST_SIZE];                                               \
    size_t idx;                                                                \
  };                                                                           \
  struct name##_t name = {.idx = 0, .objects = {0}};                           \
                                                                               \
  type pop_##name() {                                                          \
    if (name.idx == 0) {                                                       \
      assert(false);                                                           \
    }                                                                          \
    name.idx--;                                                                \
    return name.objects[name.idx];                                             \
  }                                                                            \
  bool push_##name(type obj) {                                                 \
    if (name.idx == FREELIST_SIZE) {                                           \
      return false;                                                            \
    }                                                                          \
    name.objects[name.idx] = obj;                                              \
    name.idx++;                                                                \
    return true;                                                               \
  }                                                                            \
  void name##_remove_all_from_malloc_list() {                                  \
    for (size_t i = 0; i < name.idx; i++) {                                    \
      remove_from_malloc_list(name.objects[i]);                                \
    }                                                                          \
  }
MK_FREELIST(qdbp_field_ptr, field_freelist)

void qdbp_free_field(qdbp_field_ptr field) {
  qdbp_free((void*)field);
}
qdbp_field_ptr qdbp_malloc_field() {
  if(FIELD_FREELIST && field_freelist.idx > 0) {
    return pop_field_freelist();
  }
  else {
    return (qdbp_field_ptr)qdbp_malloc(sizeof(struct qdbp_field));
  }
}
void free_fields(qdbp_prototype_ptr proto) {
  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, proto->labels, label);
  while (PValue != NULL) {
    qdbp_free_field(*PValue);
    JLN(PValue, proto->labels, label);
  }
}

MK_FREELIST(qdbp_object_ptr, freelist)

void qdbp_free_obj(qdbp_object_ptr obj) {
  if (OBJ_FREELIST && push_freelist(obj)) {

  } else {
    qdbp_free((void *)obj);
  }
}
mp_pool_t obj_pool;

qdbp_object_ptr qdbp_malloc_obj() {
  if (OBJ_FREELIST && freelist.idx > 0) {
    return pop_freelist();
  } else {
    return (qdbp_object_ptr)qdbp_malloc(sizeof(struct qdbp_object));
  }
}

// Keep track of objects for testing
struct object_list_node {
  qdbp_object_ptr obj;
  struct object_list_node *next;
};
struct object_list_node *object_list = NULL;
qdbp_object_ptr make_object(enum qdbp_object_kind kind,
                            union qdbp_object_data data) {
  qdbp_object_ptr new_obj = qdbp_malloc_obj();
  // add new_obj to object_list. Only if freeing is off otherwise
  // object might be freed before we get to it
  if (BUMP_ALLOCATOR && VERIFY_REFCOUNTS) {
    struct object_list_node *new_obj_list =
        (struct object_list_node *)malloc(sizeof(struct object_list_node));
    new_obj_list->obj = new_obj;
    new_obj_list->next = object_list;
    object_list = new_obj_list;
  }
  new_obj->refcount = 1;
  new_obj->kind = kind;
  new_obj->data = data;
  return new_obj;
}

qdbp_object_ptr empty_prototype() {
  qdbp_object_ptr obj = make_object(
      QDBP_PROTOTYPE,
      (union qdbp_object_data){.prototype = {.labels = NULL}});
  return obj;
}
qdbp_object_ptr qdbp_true() {
  return make_object(QDBP_VARIANT,
                     (union qdbp_object_data){
                         .variant = {.tag = 1, .value = empty_prototype()}});
}
qdbp_object_ptr qdbp_false() {
  return make_object(QDBP_VARIANT,
                     (union qdbp_object_data){
                         .variant = {.tag = 0, .value = empty_prototype()}});
}
/*
    ====================================================
    Reference Counting Functions
    ====================================================
*/
bool is_unique(qdbp_object_ptr obj) {
  assert(obj);
  assert(obj->refcount >= 0);
  return REFCOUNT && obj->refcount == 1;
}
void decref(qdbp_object_ptr obj) {
  assert_refcount(obj);
  obj->refcount--;
}

void del_method(qdbp_method_ptr method);
void del_prototype(qdbp_prototype_ptr proto);
void del_variant(qdbp_variant_ptr variant);
void del_obj(qdbp_object_ptr obj);
bool drop(qdbp_object_ptr obj, refcount_t cnt);

// FIXME: Remove code duplication

#define MAKE_CASES FORALL_FREELISTS(MAKE_CASE)

#define MK_CAPTURE_FREELIST(n) MK_FREELIST(qdbp_object_arr, capture_freelist##n)
FORALL_FREELISTS(MK_CAPTURE_FREELIST)


bool pop_capture_freelist(size_t size, qdbp_object_arr *capture) {
#define MAKE_CASE(n)                                                           \
  case n:                                                                      \
    if (capture_freelist##n.idx > 0) {                                         \
      *capture = pop_capture_freelist##n();                                    \
      return true;                                                             \
    }                                                                          \
    break;
  switch (size) {
    MAKE_CASES
#undef MAKE_CASE
  default:
    return false;
    break;
  }
  return false;
}

bool push_capture_freelist(size_t size, qdbp_object_arr capture) {
  if (!capture) {
    return false;
  }
#define MAKE_CASE(n)                                                           \
  case n:                                                                      \
    return push_capture_freelist##n(capture);                                  \
    break;
  switch (size) {
    MAKE_CASES
#undef MAKE_CASE
  default:
    return false;
    break;
  }
}
size_t proto_size(qdbp_prototype_ptr proto) {
  size_t size = 0;
  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, proto->labels, label);
  while (PValue != NULL) {
    size++;
    JLN(PValue, proto->labels, label);
  }
  return size;
}
void del_prototype(qdbp_prototype_ptr proto) {
  if (DYNAMIC_TYPECHECK) {
    assert(proto);
  }
  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, proto->labels, label);
  while (PValue != NULL) {
    del_method(&((*PValue)->method));
    JLN(PValue, proto->labels, label);
  }

  free_fields(proto);
  int rc;
  if (proto->labels) {
    JLFA(rc, proto->labels);
    if (rc == 0 && DYNAMIC_TYPECHECK) {
      printf("Failed to free labels");
      assert(false);
    }
  }
}

void del_variant(qdbp_variant_ptr variant) {
  assert(variant);
  drop(variant->value, 1);
}

qdbp_object_arr make_capture_arr(size_t size) {
  qdbp_object_arr capture;
  if (CAPTURE_FREELIST && pop_capture_freelist(size, &capture)) {
    return capture;
  } else {
    return (qdbp_object_ptr *)qdbp_malloc(sizeof(qdbp_object_ptr) * size);
  }
}
void free_capture_arr(qdbp_object_arr arr, size_t size) {
  if (arr) {
    if (CAPTURE_FREELIST && push_capture_freelist(size, arr)) {
      return;
    } else {
      qdbp_free(arr);
    }
  }
}
void del_method(qdbp_method_ptr method) {
  assert(method);
  for (size_t i = 0; i < method->captures_size; i++) {
    drop((method->captures[i]), 1);
  }
  if (method->captures_size > 0) {
    free_capture_arr(method->captures, method->captures_size);
  }
}

void del_obj(qdbp_object_ptr obj) {

  switch (obj->kind) {
  case QDBP_INT:
    break;
  case QDBP_FLOAT:
    break;
  case QDBP_STRING:
    qdbp_free(obj->data.s);
    break;
  case QDBP_PROTOTYPE:
    del_prototype(&(obj->data.prototype));
    break;
  case QDBP_VARIANT:
    del_variant(&(obj->data.variant));
    break;
  }
  qdbp_free_obj(obj);
}

bool drop(qdbp_object_ptr obj, refcount_t cnt) {
  assert_refcount(obj);
  if (VERIFY_REFCOUNTS) {
    assert(obj->refcount >= cnt);
  }
  if (REFCOUNT) {
    obj->refcount -= cnt;
    if (obj->refcount == 0) {
      del_obj(obj);
      return true;
    }
    return false;
  } else {
    return false;
  }
}

void obj_dup(qdbp_object_ptr obj, refcount_t cnt) {
  assert_refcount(obj);
  if (REFCOUNT) {
    obj->refcount += cnt;
  }
}

void dup_captures(qdbp_method_ptr method) {
  for (size_t i = 0; i < method->captures_size; i++) {
    obj_dup((method->captures[i]), 1);
  }
}

void dup_prototype_captures(qdbp_prototype_ptr proto) {
  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, proto->labels, label);
  while (PValue != NULL) {
    dup_captures(&((*PValue)->method));
    JLN(PValue, proto->labels, label);
  }
}
void dup_prototype_captures_except(qdbp_prototype_ptr proto, label_t except) {
  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, proto->labels, label);
  while (PValue != NULL) {
    if (label != except) {
      dup_captures(&((*PValue)->method));
    }
    JLN(PValue, proto->labels, label);
  }
}
/*
    ====================================================
    Prototype Utilities
    ====================================================
*/

void label_add(qdbp_prototype_ptr proto, label_t label, qdbp_field_ptr field) {
  Word_t *PValue;
  if (DYNAMIC_TYPECHECK) {
    void *get;
    JLG(get, proto->labels, label);
    if (get != NULL) {
      printf("Label %llu already exists in prototype\n", label);
      assert(false);
    }
  }
  JLI(PValue, proto->labels, label);
  *PValue = (uintptr_t)field;
}
void label_replace(qdbp_prototype_ptr proto, label_t label,
                   qdbp_field_ptr field) {
  if (DYNAMIC_TYPECHECK) {
    void *get;
    JLG(get, proto->labels, label);
    if (get == NULL) {
      printf("Label %llu does not exist in prototype\n", label);
      assert(false);
    }
  }
  int rc;
  JLD(rc, proto->labels, label);
  if (DYNAMIC_TYPECHECK) {
    assert(rc);
  }
  Word_t *PValue;
  JLI(PValue, proto->labels, label);
  *PValue = (uintptr_t)field;
}
qdbp_field_ptr label_get(qdbp_prototype_ptr proto, label_t label) {
  Word_t *PValue;
  if (DYNAMIC_TYPECHECK) {
    assert(proto);
    assert(proto->labels);
  }
  JLG(PValue, proto->labels, label);
  qdbp_field_ptr field = *(qdbp_field_ptr *)PValue;
  if (DYNAMIC_TYPECHECK) {
    assert(proto->labels);
    assert(field != NULL);
  }
  return field;
}

void copy_captures_except(qdbp_prototype_ptr new_prototype, label_t except) {
  // Copy all the capture arrays except the new one
  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, new_prototype->labels, label);
  while (PValue != NULL) {
    qdbp_field_ptr field = *PValue;
    if (label != except) {
      qdbp_object_arr original = field->method.captures;
      field->method.captures = make_capture_arr(field->method.captures_size);
      qdbp_memcpy(field->method.captures, original,
                  sizeof(qdbp_object_ptr) * field->method.captures_size);
    }
    JLN(PValue, new_prototype->labels, label);
  }
}

struct qdbp_prototype raw_prototype_replace(const qdbp_prototype_ptr src,
                                            const qdbp_field_ptr new_field,
                                            label_t new_label) {
  size_t src_size = proto_size(src);
  assert(src_size);
  struct qdbp_prototype new_prototype = {.labels = NULL};

  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, src->labels, label);
  while (PValue != NULL) {
    qdbp_field_ptr new_field_ptr = qdbp_malloc_field();
    label_add(&new_prototype, label, new_field_ptr);
    *new_field_ptr = **PValue;
    JLN(PValue, src->labels, label);
  }

  // overwrite the field
  *label_get(&new_prototype, new_label) = *new_field;
  copy_captures_except(&new_prototype, new_label);
  return new_prototype;
}

struct qdbp_prototype raw_prototype_extend(const qdbp_prototype_ptr src,
                                           const qdbp_field_ptr new_field,
                                           size_t new_label) {
  // copy src
  size_t src_size = proto_size(src);
  if (DYNAMIC_TYPECHECK) {
    assert(src_size <= LABEL_CNT);
  }
  struct qdbp_prototype new_prototype = {.labels = NULL};

  Word_t label = 0;
  qdbp_field_ptr *PValue;
  JLF(PValue, src->labels, label);
  while (PValue != NULL) {
    qdbp_field_ptr new_field_ptr = qdbp_malloc_field();
    *new_field_ptr = **PValue;
    label_add(&new_prototype, label, new_field_ptr);
    JLN(PValue, src->labels, label);
  }
  qdbp_field_ptr new_field_ptr = qdbp_malloc_field();
  *new_field_ptr = *new_field;
  label_add(&new_prototype, new_label, new_field_ptr);
  copy_captures_except(&new_prototype, new_label);
  return new_prototype;
}

/*
    ==========================================================
    Object Utilities. See Fig 7 of the perceus refcount paper
    ==========================================================
    The basic theory is that every function "owns" each of its args
    When the function returns, either the arg is returned(or an object that
   points to it is returned) or the arg is dropped.
*/

/* Prototype Invoke
`invoke prototype label args`
  - lookup the required method
  - dup the captures
  - get the code ptr
  - drop the prototype
  - call the code ptr
*/
qdbp_object_arr get_method(qdbp_object_ptr obj, label_t label,
                           void **code_ptr /*output param*/) {
  assert_obj_kind(obj, QDBP_PROTOTYPE);
  struct qdbp_method m = label_get(&(obj->data.prototype), label)->method;
  dup_captures(&m);
  *code_ptr = m.code;
  qdbp_object_arr ret = m.captures;
  return ret;
}

/* Prototype Extension
`extend prototype label method`
  - Do the prototype extension
  - Set the refcount to 1
  - Dup all the captures
  - Drop the old prototype
*/
qdbp_object_arr make_captures(qdbp_object_arr captures, size_t size) {
  if (size == 0) {
    return NULL;
  } else {
    qdbp_object_arr out =
        (qdbp_object_arr)qdbp_malloc(sizeof(struct qdbp_object *) * size);
    qdbp_memcpy(out, captures, sizeof(struct qdbp_object *) * size);
    return out;
  }
}

qdbp_object_ptr extend(qdbp_object_ptr obj, label_t label, void *code,
                       qdbp_object_arr captures, size_t captures_size) {
  struct qdbp_field f = {
      .method = {.captures = make_captures(captures, captures_size),
                 .captures_size = captures_size,
                 .code = code}};
  assert_obj_kind(obj, QDBP_PROTOTYPE);
  qdbp_prototype_ptr prototype = &(obj->data.prototype);
  struct qdbp_prototype new_prototype =
      raw_prototype_extend(prototype, &f, label);
  qdbp_object_ptr new_obj = make_object(
      QDBP_PROTOTYPE, (union qdbp_object_data){.prototype = new_prototype});
  dup_prototype_captures(prototype);
  drop(obj, 1);
  return new_obj;
}
/* Prototype Replacement
Identical to above except we don't dup captures of new method
`replace prototype label method`
  - Duplicate and replace the prototype
  - dup all the shared captures between the old and new prototype
  - drop prototype
*/
qdbp_object_ptr replace(qdbp_object_ptr obj, label_t label, void *code,
                        qdbp_object_arr captures, size_t captures_size) {
  assert_obj_kind(obj, QDBP_PROTOTYPE);
  struct qdbp_field f = {
      .method = {.captures = make_captures(captures, captures_size),
                 .captures_size = captures_size,
                 .code = code}};
  if (!REUSE_PROTO_REPLACE || !is_unique(obj)) {
    struct qdbp_prototype new_prototype =
        raw_prototype_replace(&(obj->data.prototype), &f, label);
    dup_prototype_captures_except(&(obj->data.prototype), label);

    qdbp_object_ptr new_obj = make_object(
        QDBP_PROTOTYPE, (union qdbp_object_data){.prototype = new_prototype});

    drop(obj, 1);
    return new_obj;
  } else {
    // find the index to replace
    qdbp_field_ptr field = label_get(&(obj->data.prototype), label);
    // reuse the field
    del_method(&(field->method));
    *field = f;
    return obj;
  }
}
/* Variant Creation
`variant label value`
  - create a new variant w/ refcount 1 in the heap
*/
qdbp_object_ptr variant_create(tag_t tag, qdbp_object_ptr value) {
  assert_refcount(value);
  qdbp_object_ptr new_obj = make_object(
      QDBP_VARIANT,
      (union qdbp_object_data){.variant = {.tag = tag, .value = value}});
  return new_obj;
}
/* Variant Pattern Matching
  - dup the variant payload
  - drop the variant
  - execute the variant stuff
*/
void decompose_variant(qdbp_object_ptr obj, tag_t *tag,
                       qdbp_object_ptr *payload) {
  assert_obj_kind(obj, QDBP_VARIANT);
  qdbp_object_ptr value = obj->data.variant.value;
  *tag = obj->data.variant.tag;
  // return value, tag
  *payload = value;
}

// Macros/Fns for the various kinds of expressions
#define DROP(v, cnt, expr) (drop(v, cnt), (expr))
#define DUP(v, cnt, expr) (obj_dup(v, cnt), (expr))
#define LET(lhs, rhs, in)                                                      \
  ((lhs = (rhs)), (in)) // assume lhs has been declared already
#define MATCH(tag1, tag2, arg, ifmatch, ifnomatch)                             \
  ((tag1) == (tag2) ? (LET(arg, payload, ifmatch)) : (ifnomatch))
qdbp_object_ptr match_failed() {
  assert(false);
  __builtin_unreachable();
}

qdbp_object_ptr qdbp_int(int64_t i) {
  qdbp_object_ptr new_obj =
      make_object(QDBP_INT, (union qdbp_object_data){.i = i});
  return new_obj;
}

qdbp_object_ptr qdbp_string(const char *src) {
  char *dest = (char *)qdbp_malloc(strlen(src) + 1);
  strcpy(dest, src);
  qdbp_object_ptr new_obj =
      make_object(QDBP_STRING, (union qdbp_object_data){.s = dest});
  return new_obj;
}

qdbp_object_ptr qdbp_float(double f) {
  qdbp_object_ptr new_obj =
      make_object(QDBP_FLOAT, (union qdbp_object_data){.f = f});
  return new_obj;
}
void check_mem() {
  // We can't do this if freeing is on cuz the objects might already be freed
  if (BUMP_ALLOCATOR && VERIFY_REFCOUNTS) {
    struct object_list_node *node = object_list;
    while (node) {
      refcount_t refcount = node->obj->refcount;
      if (refcount > 0) {
        printf("Error: refcount of %p is %lld\n", (void *)node->obj, refcount);
      }
      node = node->next;
    }
  } else {
    assert(!object_list);
  }
  // Make sure that everything that has been malloc'd has been freed
  if (CHECK_MALLOC_FREE) {
    if (OBJ_FREELIST) {
      freelist_remove_all_from_malloc_list();
    }
    if (CAPTURE_FREELIST) {
#define RM_CAPTURE_FROM_MALLOC_LIST(n)                                         \
  capture_freelist##n##_remove_all_from_malloc_list();
      FORALL_FREELISTS(RM_CAPTURE_FROM_MALLOC_LIST)
    }
    struct malloc_list_node *node = malloc_list;
    while (node) {
      printf("Error: %p was malloc'd but not freed\n", node->ptr);
      node = node->next;
    }
  }
}
void init() {
  size_t total_freelist_mem = 0;
  total_freelist_mem += sizeof(struct freelist_t);
  total_freelist_mem += sizeof(struct qdbp_object) * FREELIST_SIZE;
  for (size_t i = 0; i < NUM_FREELISTS; i++) {
    total_freelist_mem += sizeof(struct qdbp_field) * FREELIST_SIZE;
    total_freelist_mem += sizeof(qdbp_object_ptr) * FREELIST_SIZE;
  }
  printf("Total freelist mem: %zu\n", total_freelist_mem);

  bump_init();
  for (int i = 0; i < FREELIST_SIZE; i++) {
    if (!CHECK_MALLOC_FREE) {
      push_freelist(qdbp_malloc(sizeof(struct qdbp_object)));

#define PUSH_FREELIST(n)                                                       \
  push_capture_freelist##n(qdbp_malloc(sizeof(qdbp_object_ptr) * n));          
  FORALL_FREELISTS(PUSH_FREELIST)
    }
  }
}
#endif
