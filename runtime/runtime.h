#ifndef QDBP_RUNTIME_H
#define QDBP_RUNTIME_H
#include <inttypes.h>
#include <assert.h>
#include <gmp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
// config
// You could also add fsanitize=undefined
// Also could adjust inlining
#ifdef QDBP_DEBUG
const bool _QDBP_REFCOUNT = true;
const bool _QDBP_REUSE_ANALYSIS = true;
const bool _QDBP_POOL_ALLOC = false;
const bool _QDBP_ASSERTS_ENABLED = true;
#elif defined(QDBP_RELEASE)
const bool _QDBP_REFCOUNT = true;
const bool _QDBP_REUSE_ANALYSIS = true;
const bool _QDBP_POOL_ALLOC = true;
const bool _QDBP_ASSERTS_ENABLED = false;
#else
extern const bool _QDBP_REFCOUNT;
extern const bool _QDBP_REUSE_ANALYSIS;
extern const bool _QDBP_POOL_ALLOC;
extern const bool _QDBP_ASSERTS_ENABLED;
// Hashtable settings
static const size_t _QDBP_LOAD_FACTOR_NUM = 1;
static const size_t _QDBP_LOAD_FACTOR_DEN = 1;
static const size_t _QDBP_HT_DEFAULT_CAPACITY = 4;
static const size_t _QDBP_HT_DEFAULT_CAPACITY_LG2 = 2;
#endif

#define _qdbp_assert(x)          \
  do {                           \
    if (_QDBP_ASSERTS_ENABLED) { \
      assert(x);                 \
    }                            \
  } while (0)


/*
    ====================================================
    Types
    ====================================================
*/
typedef uint32_t _qdbp_label_t;
typedef uint32_t _qdbp_tag_t;
typedef intptr_t _qdbp_refcount_t;

struct _qdbp_object;

struct _qdbp_method {
  struct _qdbp_object** captures;
  void* code;
  uint32_t num_captures;
};

struct _qdbp_field {
  struct _qdbp_method method;
  _qdbp_label_t label;
};

struct _qdbp_hashtable_header {
  size_t capacity;
  size_t capacity_lg2;
  size_t size;
  size_t* directory;
};

typedef union {
  struct _qdbp_hashtable_header header;
  struct _qdbp_field field;
} _qdbp_hashtable_t;

struct _qdbp_prototype {
  _qdbp_hashtable_t* label_map;
};

struct _qdbp_variant {
  struct _qdbp_object* value;
};

struct _qdbp_string {
  char* value;
  size_t
      length;  // number of characters in string(not including null terminator)
  struct _qdbp_prototype prototype;
};

struct _qdbp_boxed_int {
  mpz_t value;
  struct _qdbp_prototype prototype;
};

union _qdbp_object_data {
  struct _qdbp_prototype prototype;
  struct _qdbp_string* string;
  struct _qdbp_boxed_int* boxed_int;
  struct _qdbp_variant variant;
};

enum _qdbp_object_kind {
  _QDBP_STRING,
  _QDBP_PROTOTYPE,
  _QDBP_BOXED_INT,
  _QDBP_VARIANT  // Must be last
};

struct __attribute__((packed)) _qdbp_object_metadata {
  _qdbp_refcount_t rc;
  _qdbp_tag_t tag;
};

struct _qdbp_object {
  struct _qdbp_object_metadata metadata;
  union _qdbp_object_data data;
};

// MUST KEEP IN SYNC WITH namesToInts.ml
enum _QDBP_ARITH_OP {
  _QDBP_PRINT = 69,
  _QDBP_EXEC = 75,
  // ===============================================================
  // END UNARY OPS
  // Start of arithmetic ops
  // ===============================================================
  _QDBP_ADD = 84,  // MUST be the first arith op after all unary ops
  _QDBP_SUB = 139,
  _QDBP_MUL = 140,
  _QDBP_DIV = 193,
  _QDBP_MOD = 254,  // MUST be the last op before all the comparison ops
  // ===============================================================
  // END arithmetic OPS
  // Start of comparison ops
  // ===============================================================
  _QDBP_EQ = 306,   // MUST be the first op after all the arithmetic ops
  _QDBP_NEQ = 355,
  _QDBP_LT = 362,
  _QDBP_GT = 447,
  _QDBP_LEQ = 455,
  _QDBP_GEQ = 461,
  _QDBP_EXCL = 504,
  _QDBP_SEND = 505,
  _QDBP_RECEIVE = 506,
  _QDBP_MAX_OP = 1000
};
// TODO: Make sure the following is complete and used as much as possible
// instead of struct _qdbp_object* use _qdbp_object_ptr for example
typedef struct _qdbp_object* _qdbp_object_ptr;
typedef struct _qdbp_object** _qdbp_object_arr;
typedef struct _qdbp_variant* _qdbp_variant_ptr;
typedef struct _qdbp_prototype* _qdbp_prototype_ptr;
typedef struct _qdbp_method* _qdbp_method_ptr;
typedef struct _qdbp_field* _qdbp_field_ptr;
typedef struct _qdbp_string* _qdbp_string_ptr;

// Reference counting
bool _qdbp_is_unique(_qdbp_object_ptr obj);
void _qdbp_incref(_qdbp_object_ptr obj, _qdbp_refcount_t amount);
void _qdbp_decref(_qdbp_object_ptr obj, _qdbp_refcount_t amount);
void _qdbp_set_refcount(_qdbp_object_ptr obj, _qdbp_refcount_t refcount);
_qdbp_refcount_t _qdbp_get_refcount(_qdbp_object_ptr obj);
void _qdbp_drop(_qdbp_object_ptr obj, _qdbp_refcount_t cnt);
void _qdbp_dup(_qdbp_object_ptr obj, _qdbp_refcount_t cnt);
void _qdbp_dup_method_captures(_qdbp_method_ptr method);
void _qdbp_dup_prototype_captures(_qdbp_prototype_ptr proto);
void _qdbp_dup_prototype_captures_except(_qdbp_prototype_ptr proto,
                                         _qdbp_label_t except);

#define _qdbp_check_refcount(obj)                                      \
  do {                                                                 \
    if (_QDBP_ASSERTS_ENABLED && obj) {                                \
      if(_qdbp_is_unboxed_int(obj)) {break;}\
      _qdbp_assert((obj));                                             \
      if (_qdbp_get_refcount(obj) <= 0) {                              \
        printf("refcount of %" PRIiPTR "\n", _qdbp_get_refcount(obj)); \
        _qdbp_assert(false);                                           \
      };                                                               \
    }                                                                  \
  } while (0)

// Hash table
_qdbp_hashtable_t* _qdbp_ht_new(size_t capacity, size_t capacity_lg2);
void _qdbp_ht_del(_qdbp_hashtable_t* table);
_qdbp_hashtable_t* _qdbp_ht_duplicate(_qdbp_hashtable_t* table);
// will hang if the label is not in the table
_qdbp_field_ptr _qdbp_ht_find(_qdbp_hashtable_t* table, _qdbp_label_t label);
// will return NULL if the label is not in the table
_qdbp_field_ptr _qdbp_ht_find_opt(_qdbp_hashtable_t* table,
                                  _qdbp_label_t label);
__attribute__((warn_unused_result)) _qdbp_hashtable_t* _qdbp_ht_insert(
    _qdbp_hashtable_t* table, const _qdbp_field_ptr fld);
size_t _qdbp_ht_size(_qdbp_hashtable_t* table);
#define _QDBP_HT_ITER(ht, fld, tmp)                                       \
  size_t tmp;                                                             \
  _qdbp_field_ptr fld;                                                    \
  for (tmp = 0; ht && tmp < (ht)->header.size &&                          \
                (fld = &((ht)[(ht)->header.directory[tmp]].field), true); \
       tmp++)
// smallint math
intptr_t _qdbp_sign_extend(uintptr_t a);
_qdbp_object_ptr _qdbp_smallint_add(uintptr_t a, uintptr_t b);
_qdbp_object_ptr _qdbp_smallint_sub(uintptr_t a, uintptr_t b);
_qdbp_object_ptr _qdbp_smallint_mul(uintptr_t a, uintptr_t b);
_qdbp_object_ptr _qdbp_smallint_div(uintptr_t a, uintptr_t b);
_qdbp_object_ptr _qdbp_smallint_mod(uintptr_t a, uintptr_t b);
typedef typeof(_qdbp_smallint_add) _qdbp_smallint_arith_fn;
// bigint math
typedef typeof(mpz_add) _qdbp_bigint_arith_fn;
_qdbp_object_ptr _qdbp_int_binary_op(_qdbp_object_ptr l, _qdbp_object_ptr r,
                                     enum _QDBP_ARITH_OP op);
_qdbp_object_ptr _qdbp_int_unary_op(_qdbp_object_ptr obj,
                                    enum _QDBP_ARITH_OP op);
// Memory and setup/teardown
extern pthread_attr_t _qdbp_thread_attr;
#define _QDBP_STR_INTERNAL(x) #x
#define _QDBP_STR(x) _QDBP_STR_INTERNAL(x)

void* _qdbp_malloc(size_t size);
void* _qdbp_realloc(void* ptr, size_t size);
void _qdbp_free(void* ptr);
void _qdbp_memcpy(void* dest, const void* src, size_t n);
void _qdbp_init();
void _qdbp_cleanup();
int _qdbp_get_return(_qdbp_object_ptr result);
_qdbp_object_ptr _qdbp_exit_int();

// _qdbp_malloc_<type> allocates the physical memory for the object
// _qdbp_free_<type> free the physical memory of the object

_qdbp_object_ptr _qdbp_obj_malloc();
void _qdbp_obj_free(_qdbp_object_ptr obj);

_qdbp_string_ptr _qdbp_qstring_malloc();
void _qdbp_qstring_free(_qdbp_string_ptr str);

_qdbp_object_arr _qdbp_capture_arr_malloc(size_t size);
void _qdbp_capture_arr_free(_qdbp_object_arr arr);

struct _qdbp_boxed_int* _qdbp_boxed_int_malloc();
void _qdbp_boxed_int_free(struct _qdbp_boxed_int* i);

_qdbp_hashtable_t* _qdbp_ht_malloc(size_t capacity_lg2);
void _qdbp_ht_free(_qdbp_hashtable_t* table);

// TODO: Use _qdbp_boxed_int_ptr instead
size_t * _qdbp_directory_malloc(size_t capacity);
void _qdbp_directory_free(size_t * directory);

char *_qdbp_cstring_malloc(size_t length);
void _qdbp_cstring_free(char *);

// _qdbp_del_<type> recursively drops the object's children
// then free the physical memory of the object
void _qdbp_del_fields(_qdbp_prototype_ptr proto);
void _qdbp_del_prototype(_qdbp_prototype_ptr proto);
void _qdbp_del_variant(_qdbp_variant_ptr variant);
void _qdbp_del_method(_qdbp_method_ptr method);
void _qdbp_del_obj(_qdbp_object_ptr obj);

// Object creation
// Each of these functions creates the object in memory and initializes its
// refcount to 1 and its value accordingly
_qdbp_object_ptr _qdbp_make_object(_qdbp_tag_t tag,
                                   union _qdbp_object_data data);
_qdbp_object_ptr _qdbp_empty_prototype();
_qdbp_object_ptr _qdbp_make_string(const char* cstr, size_t length);
_qdbp_object_ptr _qdbp_true();
_qdbp_object_ptr _qdbp_false();
_qdbp_object_ptr _qdbp_bool(bool value);
_qdbp_object_ptr _qdbp_abort();
_qdbp_object_ptr _qdbp_match_failed();
_qdbp_object_ptr _qdbp_make_boxed_int();
_qdbp_object_ptr _qdbp_make_boxed_int_from_cstr(const char* str);
// Object initialization
// Each of these functions initializes the object's value
void _qdbp_init_field(_qdbp_field_ptr field, _qdbp_label_t label,
                      struct _qdbp_object** captures, void* code,
                      uint32_t num_captures);
// Prototypes
size_t _qdbp_prototype_size(_qdbp_prototype_ptr proto);
void _qdbp_label_add(_qdbp_prototype_ptr proto, _qdbp_field_ptr field,
                     size_t default_capacity, size_t default_capacity_lg2);
_qdbp_field_ptr _qdbp_label_get(_qdbp_prototype_ptr proto, _qdbp_label_t label);
_qdbp_field_ptr _qdbp_label_get_opt(_qdbp_prototype_ptr proto,
                                    _qdbp_label_t label);
void _qdbp_copy_prototype(_qdbp_prototype_ptr src, _qdbp_prototype_ptr dest);
// For each label in `new_prototype`, re-malloc the capture array and copy the
// captures from `old_prototype` to `new_prototype` except for the label
// `except`
void _qdbp_make_fresh_captures_except(_qdbp_prototype_ptr new_prototype,
                                      _qdbp_label_t except);
_qdbp_object_arr _qdbp_copy_captures(_qdbp_object_arr captures, size_t size);
// returns a pointer to the method's capture array and stores its fn pointer in
// `code_ptr`
_qdbp_object_arr _qdbp_get_method(_qdbp_object_ptr obj, _qdbp_label_t label,
                                  void** code /*output param*/);
_qdbp_object_ptr _qdbp_extend(_qdbp_object_ptr obj, _qdbp_label_t label,
                              void* code, _qdbp_object_arr captures,
                              size_t captures_size, size_t default_capacity, size_t default_capacity_lg2);

_qdbp_object_ptr _qdbp_replace(_qdbp_object_ptr obj, _qdbp_label_t label,
                               void* code, _qdbp_object_arr captures,
                               size_t captures_size);

// invoke the `label` method of `receiver` with `arg0` and `arg1` as arguments
_qdbp_object_ptr _qdbp_invoke_1(_qdbp_object_ptr receiver, _qdbp_label_t label,
                                _qdbp_object_ptr arg0);
_qdbp_object_ptr _qdbp_invoke_2(_qdbp_object_ptr receiver, _qdbp_label_t label,
                                _qdbp_object_ptr arg0, _qdbp_object_ptr arg1);
void* _qdbp_invoke_excl(void* receiver_voidptr);
_qdbp_object_ptr _qdbp_invoke_excl_parallel(_qdbp_object_ptr receiver);

// ints
bool _qdbp_is_unboxed_int(_qdbp_object_ptr obj);
_qdbp_object_ptr _qdbp_make_unboxed_int(uintptr_t value);
uintptr_t _qdbp_get_unboxed_int(_qdbp_object_ptr obj);
bool _qdbp_is_boxed_int(_qdbp_object_ptr obj);
// strings
_qdbp_object_ptr _qdbp_string_unary_op(_qdbp_object_ptr obj,
                                       enum _QDBP_ARITH_OP op);
_qdbp_object_ptr _qdbp_string_binary_op(_qdbp_object_ptr l, _qdbp_object_ptr r,
                                        enum _QDBP_ARITH_OP op);
// Tags and Variants
enum _qdbp_object_kind _qdbp_get_kind(_qdbp_object_ptr obj);
void _qdbp_set_tag(_qdbp_object_ptr o, _qdbp_tag_t t);
_qdbp_tag_t _qdbp_get_tag(_qdbp_object_ptr o);
_qdbp_object_ptr _qdbp_variant_create(_qdbp_tag_t tag, _qdbp_object_ptr value);
void _qdbp_decompose_variant(_qdbp_object_ptr obj, _qdbp_tag_t* tag,
                             _qdbp_object_ptr* payload);
#define _qdbp_assert_kind(obj, k)             \
  do {                                        \
    if (obj && _QDBP_ASSERTS_ENABLED) {       \
      _qdbp_check_refcount(obj);              \
      _qdbp_assert(_qdbp_get_kind(obj) == k); \
    }                                         \
  } while (0)

// Macros for the various kinds of expressions
#define _QDBP_DROP(v, cnt, expr) (_qdbp_drop((v), (cnt)), (expr))
#define _QDBP_DUP(v, cnt, expr) (_qdbp_dup((v), (cnt)), (expr))
#define _QDBP_LET(lhs, rhs, in) \
  ((lhs = (rhs)), (in))  // assume lhs has been declared already
#define _QDBP_MATCH(tag1, tag2, arg, ifmatch, ifnomatch) \
  ((tag1) == (tag2) ? (_QDBP_LET((arg), (payload), (ifmatch))) : (ifnomatch))

#endif