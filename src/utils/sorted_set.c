
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "xxhash/xxhash.h"
#include "uthash.h"
#include "minheap/heap.h"

#include "sorted_set.h"


struct entry_data {
    char *str;
    size_t len;
};

struct sorted_set {
    heap *heap;
    struct hash_node *hash_tbl;
};

struct hash_node {
    char *member;  // key
    heap_entry *val;
    UT_hash_handle hh;         /* makes this structure hashable */
};

struct entry_data *entry_data_construct(const char *member, size_t len)
{
    struct entry_data *id = (struct entry_data *)malloc(sizeof(struct entry_data));

    id->str = (char *)malloc(len + 1);
    memcpy(id->str, member, len);
    id->str[len] = '\0';
    id->len = len + 1;
    return id;
}

void entry_data_destroy(struct entry_data *id)
{
    if (id == NULL) {
        return;
    }
    free(id->str);
    free(id);
}

unsigned sorted_set_entry_get_key(heap_entry *entry)
{
    return *(unsigned *)entry->key;
}

struct entry_data *sorted_set_entry_get_val(heap_entry *entry)
{
    return entry->value;
}

static bool cmp_int(void *aa, void *bb)
{
    if (sorted_set_entry_get_key(aa) < sorted_set_entry_get_key(bb)) {
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

heap_entry *sorted_set_find_entry_with_member(const struct sorted_set *ss, const char *member, size_t member_size)
{
    struct hash_node *hash_tbl = ss->hash_tbl;
    struct hash_node *s = NULL;
    HASH_FIND(hh, hash_tbl, member, member_size, s);

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
    unsigned count_ret = sorted_set_entry_get_key(entry);
    struct entry_data *tmp_data = sorted_set_entry_get_val(entry);

    struct hash_node *s = NULL;
    HASH_FIND(hh, hash_tbl, tmp_data->str, tmp_data->len - 1, s);
    if (s == NULL) {
        return 0;
    }

    HASH_DEL(hash_tbl, s);
    free(s);
    ss->hash_tbl = hash_tbl;

    entry_data_destroy(tmp_data);
    free(entry->key);
    free(entry);

    return count_ret;
}

bool sorted_set_check_is_full(const struct sorted_set *ss)
{
    return ss->heap->cur_size >= ss->heap->max_size;
}

void sorted_set_insert(struct sorted_set *ss, const char *member, size_t member_size, unsigned cnt)
{
    if (sorted_set_check_is_full(ss)) {
        if (cnt <= sorted_set_get_min_score(ss)) {
            return;
        }
        sorted_set_pop(ss);
    }
    unsigned *tmp = (unsigned *)malloc(sizeof(unsigned));
    *tmp = cnt;
    struct entry_data *tmpId = entry_data_construct(member, member_size); 
    heap_entry *node = (heap_entry *)malloc(sizeof(heap_entry));
    node->key = tmp;
    node->value = tmpId;

    push_heap(ss->heap, (void *)node);

    struct hash_node *hash_tbl = ss->hash_tbl;
    struct hash_node *s = (struct hash_node *)malloc(sizeof(struct hash_node));
    s->member = sorted_set_entry_get_val(node)->str;
    s->val = (node == NULL) ? NULL : node;
    HASH_ADD_KEYPTR(hh, hash_tbl, s->member, member_size, s);
    ss->hash_tbl = hash_tbl;
}

unsigned sorted_set_get_min_score(const struct sorted_set *ss)
{   
    heap *heap = ss->heap;
    if (heap->cur_size == 0) {
        return 0;
    }
    heap_entry *ret = (heap_entry *)(heap->nodes[0]);

    unsigned* key = ret->key;
    return *key;
}

unsigned sorted_set_get_member_score(const struct sorted_set *ss, const char *member, size_t member_size)
{
    if (sorted_set_cardinality(ss) == 0) {
        return 0;
    }
    heap_entry *entry = sorted_set_find_entry_with_member(ss, member, member_size);
    if (entry == NULL) {
        return 0;
    }

    return sorted_set_entry_get_key(entry);
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

void sorted_set_incrby(struct sorted_set *ss, const char *member, size_t member_size, unsigned score)
{
    heap_entry *entry = sorted_set_find_entry_with_member(ss, member, member_size);
    if (entry == NULL) {
        sorted_set_insert(ss, member, member_size, score);
        return;
    }
    sorted_set_entry_set_key(ss, entry, sorted_set_entry_get_key(entry) + score);
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

void sorted_set_dump(const struct sorted_set *ss, unsigned *scores_out, char **members_out, size_t *members_len_out)
{
    struct minheap *h = ss->heap;
    tmp_heap_node *tmp_nodes = (tmp_heap_node *)malloc(sizeof(tmp_heap_node) * h->cur_size);
    for (int i = 0; i < h->cur_size; i++) {
        heap_entry *entry = (heap_entry *)h->nodes[i];
        tmp_nodes[i].key = sorted_set_entry_get_key(entry);
        tmp_nodes[i].val = sorted_set_entry_get_val(entry);
    }
    qsort(tmp_nodes, h->cur_size, sizeof(tmp_heap_node), cmp_tmp_heap_node);
    for (int i = 0; i < h->cur_size; i++) {
        scores_out[i] = tmp_nodes[i].key;
        members_out[i] = tmp_nodes[i].val->str;
        members_len_out[i] = tmp_nodes[i].val->len;
    }
    free(tmp_nodes);
}

void sorted_set_print(const struct sorted_set *ss)
{
    heap *heap = ss->heap;
    printf("-----------------------------------------\n-------------------------------print heap---------------\n");
    heap_entry *node;

    for (int i = 0; i < heap->cur_size; i++) {
        node = (heap_entry *)heap->nodes[i];
        struct entry_data *id = node->value;
        unsigned *key = (unsigned *)node->key;

        printf("print entry: member: %s, score %u\n", id->str, *key);
    }
    printf("-------------------------------print end---------------\n");
}
