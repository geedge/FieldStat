
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "my_ut_hash_inner.h"
#include "xxhash/xxhash.h"
#include "minheap/heap.h"

#include "sorted_set.h"
#include "my_ut_hash.h"


struct entry_data {
    struct tag_hash_key *key;

    void *user_data;
};

typedef void (*free_method)(void *);
struct sorted_set {
    heap *heap;
    struct hash_node *hash_tbl;

    free_method free_user_data_func;
};

struct hash_node {
    struct tag_hash_key *key; // value is equal to entry_data::key(point to the same memory)
    heap_entry *val;
    UT_hash_handle hh;
};

struct entry_data *entry_data_construct(const struct tag_hash_key *tag)
{
    struct entry_data *entry_data = (struct entry_data *)malloc(sizeof(struct entry_data));

    entry_data->key = tag_hash_key_copy(tag);
    entry_data->user_data = NULL;
    return entry_data;
}

void entry_data_destroy(struct entry_data *entry_data, void (*free_method)(void *))
{
    if (entry_data == NULL) {
        return;
    }
    tag_hash_key_free(entry_data->key);
    if (entry_data->user_data != NULL && free_method != NULL) {
        free_method(entry_data->user_data);
    }
    free(entry_data);
}

unsigned sorted_set_entry_get_count(heap_entry *entry)
{
    return *(unsigned *)entry->key;
}

struct entry_data *sorted_set_entry_get_data(heap_entry *entry)
{
    return entry->value;
}

static bool cmp_int(void *aa, void *bb)
{
    if (sorted_set_entry_get_count(aa) < sorted_set_entry_get_count(bb)) {
        return true;
    }
    return false;
}

static int my_entry_get_index(void *a)
{
    return ((heap_entry *)a)->index;
}

void my_entry_set_index(int index, void *a)
{
    ((heap_entry *)a)->index = index;
}

void *my_entry_copy(const void *a)
{
    heap_entry *entry = (heap_entry *)a;
    heap_entry *new_entry = (heap_entry *)malloc(sizeof(heap_entry));

    unsigned *tmp_cnt = (unsigned *)malloc(sizeof(unsigned));
    *tmp_cnt = *(unsigned *)entry->key;
    new_entry->key = tmp_cnt;

    struct entry_data *data_src = sorted_set_entry_get_data(entry);
    struct entry_data *data_dst = entry_data_construct(data_src->key);
    data_dst->user_data = (int *)malloc(sizeof(int));
    *(int *)data_dst->user_data = *(int *)data_src->user_data;

    new_entry->value = data_dst;
    my_entry_set_index(entry->index, new_entry);
    return new_entry;
}

struct sorted_set *sorted_set_new(int set_size)
{
    struct sorted_set *ret = (struct sorted_set *)malloc(sizeof(struct sorted_set));
    ret->hash_tbl = NULL;
    heap *h = (heap *)malloc(sizeof(heap));
    init_heap(h, set_size, cmp_int, my_entry_get_index, my_entry_set_index);

    ret->heap = h;
    ret->free_user_data_func = NULL;
    return ret;
}

void sorted_set_free(struct sorted_set *ss)
{
    struct hash_node *tbl = ss->hash_tbl;
    struct hash_node *h_node, *tmp;
    HASH_ITER(hh, tbl, h_node, tmp) {
        HASH_DEL(tbl, h_node);  
        free(h_node);
    }

    heap *heap = ss->heap;
    heap_entry *node;
    for (int i = 0; i < heap->cur_size; i++) {
        node = (heap_entry *)heap->nodes[i];
        entry_data_destroy(node->value, ss->free_user_data_func);
        free(node->key);
        free(node);
    }
    free(heap->nodes);
    free(heap);

    free(ss);
}

heap_entry *sorted_set_find_entry(const struct sorted_set *ss, const struct tag_hash_key *tag)
{
    struct hash_node *hash_tbl = ss->hash_tbl;
    struct hash_node *s = NULL;
    HASH_FIND_TAG(hash_tbl, tag, s);

    if (s == NULL) {
        return NULL;
    }
    return (heap_entry *)s->val;
}

unsigned sorted_set_pop(struct sorted_set *ss)
{
    struct hash_node *hash_tbl = ss->hash_tbl;
    heap_entry *entry = (heap_entry *)pop_heap(ss->heap);
    if (entry == NULL) {
        return 0;
    }
    unsigned count_ret = sorted_set_entry_get_count(entry);
    struct entry_data *tmp_data = sorted_set_entry_get_data(entry);

    struct hash_node *s = NULL;
    HASH_FIND_TAG(hash_tbl, tmp_data->key, s);
    if (s == NULL) {
        return 0;
    }

    HASH_DEL(hash_tbl, s);
    free(s);
    ss->hash_tbl = hash_tbl;

    entry_data_destroy(tmp_data, ss->free_user_data_func);
    free(entry->key);
    free(entry);

    return count_ret;
}

unsigned sorted_set_pop_and_record_popped_back(struct sorted_set *ss, void *popped_user_data, size_t popped_user_data_size)
{
    struct hash_node *hash_tbl = ss->hash_tbl;
    heap_entry *entry = (heap_entry *)pop_heap(ss->heap);
    if (entry == NULL) {
        return 0;
    }
    unsigned count_ret = sorted_set_entry_get_count(entry);
    struct entry_data *tmp_data = sorted_set_entry_get_data(entry);

    struct hash_node *s = NULL;
    HASH_FIND_TAG(hash_tbl, tmp_data->key, s);
    if (s == NULL) {
        return 0;
    }

    HASH_DEL(hash_tbl, s);
    free(s);
    ss->hash_tbl = hash_tbl;

    if (tmp_data->user_data != NULL) {
        memcpy(popped_user_data, tmp_data->user_data, popped_user_data_size);
    }

    entry_data_destroy(tmp_data, ss->free_user_data_func);
    free(entry->key);
    free(entry);

    return count_ret;
}

bool sorted_set_check_is_full(const struct sorted_set *ss)
{
    return ss->heap->cur_size >= ss->heap->max_size;
}

void sorted_set_insert_to_available_heap(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned cnt)
{
    unsigned *tmp_cnt = (unsigned *)malloc(sizeof(unsigned));
    *tmp_cnt = cnt;
    struct entry_data *tmp_data = entry_data_construct(tag);
    heap_entry *node = (heap_entry *)malloc(sizeof(heap_entry));
    node->key = tmp_cnt;
    node->value = tmp_data;

    push_heap(ss->heap, (void *)node);

    struct hash_node *hash_tbl = ss->hash_tbl;
    struct hash_node *s = (struct hash_node *)malloc(sizeof(struct hash_node));
    s->key = sorted_set_entry_get_data(node)->key;
    s->val = node;
    HASH_ADD_TAG(hash_tbl, key, s);
    ss->hash_tbl = hash_tbl;
}

int sorted_set_insert(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned cnt)
{
    if (sorted_set_check_is_full(ss)) {
        if (cnt <= sorted_set_get_min_count(ss)) {
            return 0;
        }
        sorted_set_pop(ss);
    }
    sorted_set_insert_to_available_heap(ss, tag, cnt);
    return 1;
}

int sorted_set_insert_and_record_popped_back(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned cnt, void *popped_user_data, size_t popped_user_data_size)
{
    bool popped = false;
    if (sorted_set_check_is_full(ss)) {
        if (cnt <= sorted_set_get_min_count(ss)) {
            return 0;
        }
        sorted_set_pop_and_record_popped_back(ss, popped_user_data, popped_user_data_size);
        popped = true;
    }
    sorted_set_insert_to_available_heap(ss, tag, cnt);
    return popped ? 2 : 1;
}

unsigned sorted_set_get_min_count(const struct sorted_set *ss)
{   
    heap *heap = ss->heap;
    if (heap->cur_size == 0) {
        return 0;
    }
    heap_entry *ret = (heap_entry *)(heap->nodes[0]);

    unsigned* key = ret->key;
    return *key;
}

unsigned sorted_set_get_count(const struct sorted_set *ss, const struct tag_hash_key *tag)
{
    if (sorted_set_cardinality(ss) == 0) {
        return 0;
    }
    heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        return 0;
    }

    return sorted_set_entry_get_count(entry);
}

int sorted_set_cardinality(const struct sorted_set *ss)
{
    return ss->heap->cur_size;
}

void sorted_set_entry_set_key(struct sorted_set *ss, heap_entry *entry, unsigned count)
{
    *(unsigned *)(entry->key) = count;

    adjust_heap_node(ss->heap, entry);
}

void sorted_set_incrby(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned count)
{
    heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        sorted_set_insert(ss, tag, count);
        return;
    }
    sorted_set_entry_set_key(ss, entry, sorted_set_entry_get_count(entry) + count);
}

void sorted_set_hook_function_of_free_user_data(struct sorted_set *ss, void (*free_method)(void *))
{
    ss->free_user_data_func = free_method;
}

void **sorted_set_get_pointer_to_user_data(struct sorted_set *ss, const struct tag_hash_key *tag)
{
    heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        return NULL;
    }
    struct entry_data *heap_data = sorted_set_entry_get_data(entry);

    return &(heap_data->user_data);
}

int sorted_set_insert_user_data(struct sorted_set *ss, const struct tag_hash_key *tag, void *user_data)
{
    heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        return -1;
    }
    struct entry_data *heap_data = sorted_set_entry_get_data(entry);
    heap_data->user_data = user_data;
    return 0;
}

typedef struct {
    unsigned key;
    struct entry_data *val;
} tmp_heap_node;

int cmp_tmp_heap_node(const void *a, const void *b)
{
    tmp_heap_node *aa = (tmp_heap_node *)a;
    tmp_heap_node *bb = (tmp_heap_node *)b;

    if (aa->key < bb->key) {
        return -1;
    } else if (aa->key > bb->key) {
        return 1;
    } else {
        return 0;
    }
}

void sorted_set_dump(const struct sorted_set *ss, unsigned *scores_out, struct tag_hash_key **tags_out, void **user_data_out)
{
    struct minheap *h = ss->heap;
    tmp_heap_node *tmp_nodes = (tmp_heap_node *)malloc(sizeof(tmp_heap_node) * h->cur_size);
    for (int i = 0; i < h->cur_size; i++) {
        heap_entry *entry = (heap_entry *)h->nodes[i];
        tmp_nodes[i].key = sorted_set_entry_get_count(entry);
        tmp_nodes[i].val = sorted_set_entry_get_data(entry);
    }
    qsort(tmp_nodes, h->cur_size, sizeof(tmp_heap_node), cmp_tmp_heap_node);
    for (int i = 0; i < h->cur_size; i++) {
        scores_out[i] = tmp_nodes[i].key;
        tags_out[i] = tmp_nodes[i].val->key;
        user_data_out[i] = tmp_nodes[i].val->user_data;
    }
    free(tmp_nodes);
}

struct sorted_set *sorted_set_copy(const struct sorted_set *ss)
{
    struct sorted_set *ret = sorted_set_new(ss->heap->max_size);
    sorted_set_hook_function_of_free_user_data(ret, ss->free_user_data_func);
    min_heap_copy(ret->heap, ss->heap, my_entry_copy);

    return ret;
}