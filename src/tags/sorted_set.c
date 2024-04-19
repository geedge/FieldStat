
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "my_ut_hash_inner.h"
#include "xxhash/xxhash.h"
#include "minheap/heap.h"

#include "sorted_set.h"
#include "my_ut_hash.h"

#define assert(s) do { if (!(s)) { printf("assert failed: %s\n", #s);   \
        *((int *)0) = 0;   \
    } \
    } while (0)

struct entry_data {
    struct tag_hash_key *key;

    int cell_id;
};

struct sorted_set {
    heap *heap;
    struct hash_node *hash_tbl;

    int n_living_entry;
};

struct hash_node {
    struct tag_hash_key *key; // value is equal to entry_data::key(point to the same memory)
    heap_entry *val;
    UT_hash_handle hh;
};

struct entry_data *entry_data_construct(const struct tag_hash_key *tag, int cell_id)
{
    assert(cell_id >= 0);
    assert(tag != NULL);
    long long tmp_v = tag->hashv;
    assert(tmp_v >= 0);

    struct entry_data *entry_data = (struct entry_data *)malloc(sizeof(struct entry_data));

    entry_data->key = tag_hash_key_copy(tag);
    entry_data->cell_id = cell_id;
    return entry_data;
}

void entry_data_destroy(struct entry_data *entry_data)
{
    if (entry_data == NULL) {
        return;
    }
    tag_hash_key_free(entry_data->key);
    free(entry_data);
}

unsigned long long sorted_set_entry_get_count(const heap_entry *entry)
{
    unsigned long long count = *(unsigned long long *)entry->key;
    if (count == 0) {
        return 0;
    }
    return count - 1; // sorted set will let the count start from 1, 0 for dying entry.
}

struct entry_data *sorted_set_entry_get_data(const heap_entry *entry)
{
    assert(entry != NULL);
    assert(entry->value != NULL);

    const struct entry_data * tmp_val = (struct entry_data *)entry->value;
    assert(tmp_val->key != NULL);
    long long tmp_v = tmp_val->key->hashv;
    assert(tmp_v >= 0);

    assert(tmp_val->cell_id >= 0);
    return entry->value;
}

/*
    dying: To reduce the time of HASH_ADD, and to support merely recording a key without any value, give a dying status.
*/
bool sorted_set_entry_dying(const heap_entry *entry)
{
    unsigned long long count = *(unsigned long long *)entry->key;
    return count == 0;    
}

// cppcheck-suppress [constParameterCallback, unmatchedSuppression]
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

struct sorted_set *sorted_set_new(int set_size)
{
    struct sorted_set *ret = (struct sorted_set *)malloc(sizeof(struct sorted_set));
    ret->hash_tbl = NULL;
    ret->n_living_entry = 0;
    heap *h = (heap *)malloc(sizeof(heap));
    init_heap(h, set_size, cmp_int, my_entry_get_index, my_entry_set_index);

    ret->heap = h;
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
        entry_data_destroy(node->value);
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

void sorted_set_entry_set_key(struct sorted_set *ss, heap_entry *entry, unsigned long long count)
{
    *(unsigned long long *)(entry->key) = count;

    adjust_heap_node(ss->heap, entry);
}

unsigned long long sorted_set_pop_and_record_popped_back(struct sorted_set *ss, int *popped_cell_id)
{
    struct hash_node *hash_tbl = ss->hash_tbl;
    heap_entry *entry = (heap_entry *)pop_heap(ss->heap);
    if (entry == NULL) {
        return 0;
    }
    unsigned long long count_ret = sorted_set_entry_get_count(entry);
    struct entry_data *tmp_data = sorted_set_entry_get_data(entry);

    struct hash_node *s = NULL;
    HASH_FIND_TAG(hash_tbl, tmp_data->key, s);
    if (s == NULL) {
        return 0;
    }

    HASH_DEL(hash_tbl, s);
    free(s);
    ss->hash_tbl = hash_tbl;

    if (sorted_set_entry_dying(entry)) {
        *popped_cell_id = -1;
    } else {
        *popped_cell_id = tmp_data->cell_id;
        ss->n_living_entry--;
    }

    entry_data_destroy(tmp_data);
    free(entry->key);
    free(entry);

    return count_ret;
}

unsigned long long sorted_set_pop(struct sorted_set *ss)
{
    int dummy;
    unsigned long long count_ret = sorted_set_pop_and_record_popped_back(ss, &dummy);
    
    return count_ret;
}

bool sorted_set_check_is_full(const struct sorted_set *ss)
{
    return ss->heap->cur_size >= ss->heap->max_size;
}

unsigned long long safe_add(unsigned long long a, unsigned long long b)
{
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }
    return a + b;
}

void sorted_set_insert_to_available_heap(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned long long cnt, int cell_id)
{
    cnt = safe_add(cnt, 1); // sorted set will let the count start from 1, 0 for dying entry.
    unsigned long long *tmp_cnt = (unsigned long long*)malloc(sizeof(unsigned long long));
    *tmp_cnt = cnt;
    struct entry_data *tmp_data = entry_data_construct(tag, cell_id);
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

    ss->n_living_entry++;
}

int sorted_set_insert(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned long long cnt, int cell_id)
{
    assert(cell_id >= 0);
    assert(tag != NULL);
    long long tmp_v = tag->hashv;
    assert(tmp_v >= 0);

    if (sorted_set_check_is_full(ss)) {
        if (cnt <= sorted_set_get_min_count(ss)) {
            return 0;
        }
        sorted_set_pop(ss);
    }
    sorted_set_insert_to_available_heap(ss, tag, cnt, cell_id);
    return 1;
}

int sorted_set_insert_and_record_popped_back(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned long long cnt, int cell_id, int *popped_user_data)
{
    assert(cell_id >= 0);
    assert(tag != NULL);
    long long tmp_v = tag->hashv;
    assert(tmp_v >= 0);

    // if there is a dying record, reborn it to use.
    heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry != NULL) {
        if (!sorted_set_entry_dying(entry)) {
            return -1;
        }
        
        sorted_set_entry_set_key(ss, entry, safe_add(cnt, 1)); // sorted set will let the count start from 1, 0 for dying entry.
        struct entry_data *data = sorted_set_entry_get_data(entry);
        data->cell_id = cell_id;
        ss->n_living_entry++;
        return 3;
    }

    bool popped = false;
    if (sorted_set_check_is_full(ss)) {
        if (cnt <= sorted_set_get_min_count(ss)) {
            return 0;
        }
        sorted_set_pop_and_record_popped_back(ss, popped_user_data);
        if (*popped_user_data == -1) { // popped a dying entry, not a real entry.
            popped = false;
        } else {
            popped = true;
        }
    }
    sorted_set_insert_to_available_heap(ss, tag, cnt, cell_id);
    return popped ? 2 : 1;
}

unsigned long long sorted_set_get_min_count(const struct sorted_set *ss)
{   
    heap *heap = ss->heap;
    if (heap->cur_size == 0) {
        return 0;
    }
    const heap_entry *ret = (heap_entry *)(heap->nodes[0]);

    unsigned long long count = *(unsigned long long *)ret->key;
    if (count == 0) {
        return 0;
    }
    return count - 1; // sorted set will let the count start from 1, 0 for dying entry.
}

unsigned long long sorted_set_get_count(const struct sorted_set *ss, const struct tag_hash_key *tag)
{
    if (sorted_set_cardinality(ss) == 0) {
        return 0;
    }
    const heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        return 0;
    }

    return sorted_set_entry_get_count(entry);
}

int sorted_set_get_cell_id(const struct sorted_set *ss, const struct tag_hash_key *tag)
{
    if (sorted_set_cardinality(ss) == 0) {
        return -1;
    }
    const heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        return -1;
    }
    if (sorted_set_entry_dying(entry)) {
        return -1;
    }

    return sorted_set_entry_get_data(entry)->cell_id;
}

int sorted_set_cardinality(const struct sorted_set *ss)
{
    return ss->n_living_entry;
}

int sorted_set_incrby(struct sorted_set *ss, const struct tag_hash_key *tag, unsigned long long count)
{
    heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        return -1;
    }
    unsigned long long cnt_old = sorted_set_entry_get_count(entry);
    sorted_set_entry_set_key(ss, entry, safe_add(count, cnt_old));
    return 0;
}

typedef struct {
    unsigned long long key;
    struct entry_data *val;
} tmp_heap_node;

int cmp_tmp_heap_node(const void *a, const void *b)
{
    const tmp_heap_node *aa = (tmp_heap_node *)a;
    const tmp_heap_node *bb = (tmp_heap_node *)b;

    if (aa->key < bb->key) {
        return 1;
    } else if (aa->key > bb->key) {
        return -1;
    } else {
        return 0;
    }
}

void sorted_set_dump(const struct sorted_set *ss, unsigned long long *scores_out, struct tag_hash_key **tags_out, int *user_data_out)
{
    struct minheap *h = ss->heap;
    tmp_heap_node *tmp_nodes = (tmp_heap_node *)malloc(sizeof(tmp_heap_node) * h->cur_size);
    size_t n_living_entry = 0;
    for (int i = 0; i < h->cur_size; i++) {
        const heap_entry *entry = (heap_entry *)h->nodes[i];
        if (sorted_set_entry_dying(entry)) {
            continue;
        }
        tmp_nodes[n_living_entry].key = sorted_set_entry_get_count(entry);
        tmp_nodes[n_living_entry].val = sorted_set_entry_get_data(entry);
        n_living_entry++;
    }
    assert(n_living_entry == ss->n_living_entry);
    qsort(tmp_nodes, n_living_entry, sizeof(tmp_heap_node), cmp_tmp_heap_node);
    for (int i = 0; i < n_living_entry; i++) {
        assert(tmp_nodes[i].val != NULL);
        assert(tmp_nodes[i].val->cell_id >= 0);
        assert(tmp_nodes[i].val->key != NULL);
        long long tmp_haha = tmp_nodes[i].val->key->hashv;
        assert(tmp_haha >= 0);
        
        scores_out[i] = tmp_nodes[i].key;
        tags_out[i] = tmp_nodes[i].val->key;
        user_data_out[i] = tmp_nodes[i].val->cell_id;
    }
    free(tmp_nodes);
}

struct sorted_set *sorted_set_copy(const struct sorted_set *ss)
{
    struct sorted_set *ret = sorted_set_new(ss->heap->max_size);
    
    for (int i = 0; i < ss->heap->cur_size; i++) {
        const heap_entry *entry = (heap_entry *)ss->heap->nodes[i];
        if (sorted_set_entry_dying(entry)) {
            continue;
        }
        const struct entry_data *data = sorted_set_entry_get_data(entry);
        sorted_set_insert(ret, data->key, sorted_set_entry_get_count(entry), data->cell_id);
    }

    return ret;
}

// set all the entry to dying status. They will be the first to be popped.
void sorted_set_reset(struct sorted_set *ss)
{
    struct hash_node *tbl = ss->hash_tbl;
    struct hash_node *h_node, *tmp;
    HASH_ITER(hh, tbl, h_node, tmp) {
        heap_entry *entry = (heap_entry *)h_node->val;
        *(unsigned long long *)entry->key = 0;
    }

    heap *heap = ss->heap;
    heap_entry *entry;
    for (int i = 0; i < heap->cur_size; i++) {
        entry = (heap_entry *)heap->nodes[i];
        *(unsigned long long *)entry->key = 0;
    }

    ss->n_living_entry = 0;
}

struct tag_hash_key *sorted_set_get_true_pst_tag(const struct sorted_set *ss, const struct tag_hash_key *tag)
{
    const heap_entry *entry = sorted_set_find_entry(ss, tag);
    if (entry == NULL) {
        return NULL;
    }
    if (sorted_set_entry_dying(entry)) {
        return NULL;
    }
    const struct entry_data *data = sorted_set_entry_get_data(entry);
    return data->key;
}