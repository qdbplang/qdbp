#include <stdio.h>
#include <string.h>

#include "runtime.h"
pthread_attr_t _qdbp_thread_attr;

#define CONCAT_INNER(a, b) a##b
#define CONCAT(a, b) CONCAT_INNER(a, b)

#define STACK_TY(name, type)      \
  struct CONCAT(name, _stack_t) { \
    type* data;                   \
    size_t capacity;              \
    size_t size;                  \
  };

#define STACK(name, type)              \
  STACK_TY(name, type)                 \
  static struct CONCAT(name, _stack_t) \
      CONCAT(name, _stack) = {.data = NULL, .capacity = 0, .size = 0};

#define STACK_EMPTY(name) (CONCAT(name, _stack).size == 0)
#define STACK_PUSH_IMPL(stackname, value)                                \
  do {                                                                   \
    if (stackname.capacity == stackname.size) {                          \
      stackname.capacity =                                               \
          stackname.capacity == 0 ? 1 : stackname.capacity * 2;          \
      stackname.data = _qdbp_realloc(                                    \
          stackname.data, stackname.capacity * sizeof(*stackname.data)); \
    }                                                                    \
    stackname.data[stackname.size++] = value;                            \
  } while (0)
#define STACK_PUSH(name, value) STACK_PUSH_IMPL(CONCAT(name, _stack), value)

#define STACK_POP(name)               \
  do {                                \
    _qdbp_assert(!STACK_EMPTY(name)); \
    CONCAT(name, _stack).size--;      \
  } while (0)

void noop() {}

#define STACK_PEEK(name)                                        \
  (_QDBP_ASSERTS_ENABLED ? assert(!STACK_EMPTY(name)) : noop(), \
   CONCAT(name, _stack).data[CONCAT(name, _stack).size - 1])

#define MK_MALLOC_STACK(name, type) STACK(name, type*);
#define MK_MALLOC(name, type)                     \
  type* CONCAT(name, _malloc)() {                 \
    if (!_QDBP_POOL_ALLOC || STACK_EMPTY(name)) { \
      return _qdbp_malloc(sizeof(type));          \
    } else {                                      \
      type* ptr = STACK_PEEK(name);               \
      STACK_POP(name);                            \
      return ptr;                                 \
    }                                             \
  }
#define MK_FREE(name, type)              \
  void CONCAT(name, _free)(type * ptr) { \
    if (!_QDBP_POOL_ALLOC) {             \
      _qdbp_free(ptr);                   \
      return;                            \
    }                                    \
    if (ptr == NULL) {                   \
      return;                            \
    }                                    \
    STACK_PUSH(name, ptr);               \
  }
#define MK_MALLOC_FREE(name, type) \
  MK_MALLOC_STACK(name, type);     \
  MK_MALLOC(name, type);           \
  MK_FREE(name, type);

void* _qdbp_malloc(size_t size) {
  void* ptr = malloc(size);
  return ptr;
}

void* _qdbp_realloc(void* ptr, size_t size) {
  void* new_ptr = realloc(ptr, size);
  return new_ptr;
}

void _qdbp_free(void* ptr) {
  _qdbp_assert(!_qdbp_is_unboxed_int((_qdbp_object_ptr)ptr));
  free(ptr);
}

void _qdbp_memcpy(void* dest, const void* src, size_t size) {
  memcpy(dest, src, size);
}

void _qdbp_cleanup() {
  // TODO: Make some sort of leak detection
  pthread_attr_destroy(&_qdbp_thread_attr);
  pthread_exit(NULL);
}

int _qdbp_get_return(_qdbp_object_ptr result) {
  if(_qdbp_is_unboxed_int(result)) {
    return _qdbp_get_unboxed_int(result);
  } else {
    return 0;
  }
}

static void _qdbp_free_gmp(void* ptr, size_t size) { _qdbp_free(ptr); }

static void* _qdbp_realloc_gmp(void* ptr, size_t old_size, size_t new_size) {
  void* new_ptr = _qdbp_realloc(ptr, new_size);
  return new_ptr;
}

void _qdbp_init() {
  mp_set_memory_functions(_qdbp_malloc, _qdbp_realloc_gmp, _qdbp_free_gmp);
  _qdbp_assert(_QDBP_HT_DEFAULT_CAPACITY ==
               (1 << _QDBP_HT_DEFAULT_CAPACITY_LG2));
  if (pthread_attr_init(&_qdbp_thread_attr) == -1) {
    fprintf(stderr, "pthread_attr_init");
    exit(1);
  }
  pthread_attr_setdetachstate(&_qdbp_thread_attr, PTHREAD_CREATE_DETACHED);
}

_qdbp_object_arr _qdbp_capture_arr_malloc(size_t size) {
  return _qdbp_malloc(sizeof(_qdbp_object_ptr) * size);
}

void _qdbp_capture_arr_free(_qdbp_object_arr arr) { _qdbp_free(arr); }

MK_MALLOC_FREE(_qdbp_obj, struct _qdbp_object)
MK_MALLOC_FREE(_qdbp_boxed_int, struct _qdbp_boxed_int)
MK_MALLOC_FREE(_qdbp_qstring, struct _qdbp_string)

#define REPEAT_64(M)                                           \
  M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) M(9) M(10);     \
  M(11) M(12) M(13) M(14) M(15) M(16) M(17) M(18) M(19) M(20); \
  M(21) M(22) M(23) M(24) M(25) M(26) M(27) M(28) M(29) M(30); \
  M(31) M(32) M(33) M(34) M(35) M(36) M(37) M(38) M(39) M(40); \
  M(41) M(42) M(43) M(44) M(45) M(46) M(47) M(48) M(49) M(50); \
  M(51) M(52) M(53) M(54) M(55) M(56) M(57) M(58) M(59) M(60); \
  M(61) M(62) M(63)

#define HT_STACK_NAME(size) CONCAT(ht_, CONCAT(size, _stack))
#define MK_HT_STACK(size) MK_MALLOC_FREE(HT_STACK_NAME(size), _qdbp_hashtable_t)
REPEAT_64(MK_HT_STACK)

void HT_STACK_PUSH(size_t size, _qdbp_hashtable_t* value) {
#define PUSH_CASE(size)                     \
  case size:                                \
    STACK_PUSH(HT_STACK_NAME(size), value); \
    break;

  switch (size) {
    REPEAT_64(PUSH_CASE)
    default:
      _qdbp_assert(false);
      __builtin_unreachable();
  }
}

void HT_STACK_POP(size_t size) {
#define POP_CASE(size) \
  case size:           \
    STACK_POP(HT_STACK_NAME(size)); \
    break;
  switch (size) {
    REPEAT_64(POP_CASE)
    default:
      _qdbp_assert(false);
      __builtin_unreachable();
  }
}

_qdbp_hashtable_t* HT_STACK_PEEK(size_t size) {
#define PEEK_CASE(size) \
  case size:            \
    return STACK_PEEK(HT_STACK_NAME(size));
  switch (size) {
    REPEAT_64(PEEK_CASE)
    default:
      _qdbp_assert(false);
      __builtin_unreachable();
  }
}

bool HT_STACK_EMPTY(size_t size) {
#define EMPTY_CASE(size) \
  case size:             \
    return STACK_EMPTY(HT_STACK_NAME(size));
  switch (size) {
    REPEAT_64(EMPTY_CASE)
    default:
      printf("\n%zu\n", size);
      _qdbp_assert(false);
      __builtin_unreachable();
  }
}

_qdbp_hashtable_t* _qdbp_ht_malloc(size_t capacity_lg2) {
  _qdbp_assert(capacity_lg2 < 64);
  _qdbp_assert(capacity_lg2 >= 0);
  size_t ht_bytes = ((1 << capacity_lg2) + 1) * sizeof(_qdbp_hashtable_t);
  if (HT_STACK_EMPTY(capacity_lg2)) {
    return _qdbp_malloc(ht_bytes);
  } else {
    _qdbp_hashtable_t* ret = HT_STACK_PEEK(capacity_lg2);
    HT_STACK_POP(capacity_lg2);
    return ret;
  }
}

void _qdbp_ht_free(_qdbp_hashtable_t* ht) {
  HT_STACK_PUSH(ht->header.capacity_lg2, ht);
}

void _qdbp_cstring_free(char* str) { _qdbp_free(str); }

char* _qdbp_cstring_malloc(size_t size) {
  return _qdbp_malloc(size * sizeof(char));
}

void _qdbp_directory_free(size_t* dir) { _qdbp_free(dir); }

size_t* _qdbp_directory_malloc(size_t size) {
  return _qdbp_malloc(size * sizeof(size_t));
}

_qdbp_object_ptr _qdbp_exit_int() {
  exit(0);
}