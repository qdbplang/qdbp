#ifndef HASHTABLE_C
#define HASHTABLE_C
#include "runtime.h"

__attribute__((always_inline)) size_t hashtable_size(hashtable *table) {
  if(DYNAMIC_TYPECHECK) {
    assert(table);
  }
  return table->header.size;
}

__attribute__((always_inline)) hashtable *new_ht(size_t capacity) {
  // FIXME: Put into freelist
  hashtable *table = (hashtable *)qdbp_malloc((capacity + 1) *
                                                        sizeof(hashtable), "ht");
  for(size_t i = 1; i <= capacity; i++) {
    table[i].field.method.code = NULL;
  }
  table->header.size = 0;
  table->header.capacity = capacity;
  table->header.directory = qdbp_malloc(capacity * sizeof(size_t), "ht");
  return table;
}

__attribute__((always_inline)) void del_ht(hashtable *table) {
  qdbp_free(table->header.directory);
  qdbp_free(table);
}

__attribute__((always_inline)) hashtable *ht_duplicate(hashtable *table) {
  hashtable *new_table = qdbp_malloc(
      sizeof(hashtable) + (sizeof(hashtable) * table->header.capacity), "ht");
  memcpy(new_table, table,
         sizeof(hashtable) +
             (sizeof(hashtable) * (table->header.capacity)));
  new_table->header.directory =
      qdbp_malloc(sizeof(size_t) * (table->header.capacity), "ht");
  // FIXME: Only memcpy the table->header.size components
  memcpy(new_table->header.directory, table->header.directory,
         sizeof(size_t) * table->header.size);
  return new_table;
}

__attribute__((always_inline))
// n must be a power of 2
static size_t
fast_mod(size_t x, size_t n) {
  if (DYNAMIC_TYPECHECK) {
    // check that n is a power of 2
    assert((n & (n - 1)) == 0);
  }
  return x & (n - 1);
}

__attribute__((always_inline)) qdbp_field_ptr ht_find(hashtable *table,
                                                      label_t label) {
  size_t index = fast_mod(label, table->header.capacity);
  // FIXME: Assert that label != 0 and then remove second condition
  if (DYNAMIC_TYPECHECK) {
    assert(index <= table->header.capacity);
  }
  hashtable *fields = table + 1;
  if (fields[index].field.label == label) {
    return &(fields[index].field);
  }
  index = fast_mod(index + 1, table->header.capacity);
  while (fields[index].field.label != label) {
    if (DYNAMIC_TYPECHECK) {
      assert(fields[index].field.method.code != NULL);
    }
    index = fast_mod(index + 1, table->header.capacity);
    if (DYNAMIC_TYPECHECK) {
      assert(index <= table->header.capacity);
    }
  }
  return &(fields[index].field);
}

__attribute__((always_inline)) static void
ht_insert_raw(hashtable *table, const qdbp_field_ptr fld) {
  size_t index = fast_mod(fld->label, table->header.capacity);
  hashtable *fields = table + 1;
  if (DYNAMIC_TYPECHECK) {
    assert(index <= table->header.capacity);
  }
  while (fields[index].field.method.code != NULL) {
    index = fast_mod(index + 1, table->header.capacity);
    if (DYNAMIC_TYPECHECK) {
      assert(index <= table->header.capacity);
    }
  }
  fields[index].field = *fld;
  table->header.directory[table->header.size] = index + 1;
  table->header.size++;
}

__attribute__((always_inline)) hashtable *ht_insert(hashtable *table,
                                                    const qdbp_field_ptr fld) {
  if (DYNAMIC_TYPECHECK) {
    assert(fld->method.code != NULL);
  }
  if (table->header.size * LOAD_FACTOR_NUM >= table->header.capacity * LOAD_FACTOR_DEN) {
    hashtable *new_table =
        qdbp_malloc((table->header.capacity * 2 + 1) * sizeof(hashtable), "ht");
    for(size_t i = 1; i < table->header.capacity * 2 + 1; i++) {
      new_table[i].field.method.code = NULL;
    }
    new_table->header.size = 0;
    new_table->header.capacity = table->header.capacity * 2;
    new_table->header.directory =
        qdbp_malloc(sizeof(size_t) * (new_table->header.capacity), "ht");
    size_t tmp;
    qdbp_field_ptr f;
    HT_ITER(table, f, tmp) {
        ht_insert_raw(new_table, f);
    }
    qdbp_free(table->header.directory);
    qdbp_free(table);
    table = new_table;
  }
  ht_insert_raw(table, fld);
  return table;
}

#ifdef TESTH
int main(void) {
  size_t size = 600;
  hashtable *table = new_ht(INITIAL_CAPACITY);
  for (size_t i = 0; i < size; i++) {
    struct qdbp_field f = {
        .label = i,
        .method =
            {
                .code = (void*)main,
                .captures = NULL,
                .captures_size = i + 1,
            },
    };
    table = ht_insert(table, &f);
    for (size_t j = 0; j <= i; j++) {
      assert(ht_find(table, j)->method.captures_size == j + 1);
      hashtable *table2 = ht_duplicate(table);
      assert(table2 != table);
      assert(ht_find(table2, j)->method.captures_size == j + 1);
      assert(table2 != table);
      assert(table2->header.directory != table->header.directory);
      struct qdbp_field *fld;
      size_t tmp;
      HT_ITER(table, fld, tmp) {
        assert(table->header.size == i + 1);
        assert((table)->header.directory[tmp] <= table->header.size);
        assert(tmp == fld->method.captures_size - 1);
      }
      del_ht(table2);
    }
    assert(hashtable_size(table) == i + 1);
  }
  return 0;
}

#endif

#endif