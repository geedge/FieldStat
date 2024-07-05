
#include "metric_manifest.h"

#define DEFAULT_N_METRIC 32

struct metric_manifest; // defined in metric_manifest.h

struct metric_manifest_manager {
    struct metric_manifest *hash_table;

    struct metric_manifest **manifests;
    size_t manifest_size;
    size_t manifest_cnt;
};

struct metric_manifest_manager *metric_manifest_manager_new() {
    struct metric_manifest_manager *ret = calloc(1, sizeof(struct metric_manifest_manager));

    ret->manifest_size = DEFAULT_N_METRIC;
    ret->manifests = calloc(DEFAULT_N_METRIC, sizeof(struct metric_manifest *));
    ret->manifest_cnt = 0;

    return ret;
}

void metric_manifest_manager_free(struct metric_manifest_manager *pthis) {
    struct metric_manifest *cur, *tmp;
    HASH_ITER(hh, pthis->hash_table, cur, tmp) {
        HASH_DEL(pthis->hash_table, cur);
    }

    for (size_t i = 0; i < pthis->manifest_cnt; i++) {
        metric_manifest_free(pthis->manifests[i]);
    }
    free(pthis->manifests);
    free(pthis);
}

const struct metric_manifest* metric_manifest_manager_get_by_id(const struct metric_manifest_manager *pthis, int metric_id) {
    if (metric_id >= pthis->manifest_cnt || metric_id < 0) {
        return NULL;
    }
    return pthis->manifests[metric_id];
}

const struct metric_manifest* metric_manifest_manager_get_by_name(const struct metric_manifest_manager *pthis, const char *name) {
    struct metric_manifest *manifest = NULL;
    HASH_FIND_STR(pthis->hash_table, name, manifest);
    return manifest;
}

void add_manifest_to_position(struct metric_manifest_manager *pthis, struct metric_manifest *manifest, int metric_id)
{
    if (metric_id >= pthis->manifest_size) {
        pthis->manifests = realloc(pthis->manifests, sizeof(struct metric_manifest *) * pthis->manifest_size * 2);
        memset(pthis->manifests + pthis->manifest_size, 0, sizeof(struct metric_manifest *) * (pthis->manifest_size));
        pthis->manifest_size *= 2;
    }

    pthis->manifests[metric_id] = manifest;
    if (metric_id >= pthis->manifest_cnt) {
        pthis->manifest_cnt = metric_id + 1;
    }

    HASH_ADD_KEYPTR(hh, pthis->hash_table, manifest->name, strlen(manifest->name), manifest);
}

int metric_manifest_manager_add(struct metric_manifest_manager *pthis, struct metric_manifest *manifest) {
    struct metric_manifest *tmp;
    HASH_FIND_STR(pthis->hash_table, manifest->name, tmp);
    if (tmp != NULL) {
        return -1;
    }

    int id = pthis->manifest_cnt;
    add_manifest_to_position(pthis, manifest, id);

    manifest->id = id;
    return id;
}

const struct metric_manifest **metric_manifest_manager_list(const struct metric_manifest_manager *pthis, size_t *n_manifest) {
    *n_manifest = pthis->manifest_cnt;
    return (const struct metric_manifest **)pthis->manifests;
}

bool metric_manifest_manager_equal(const struct metric_manifest_manager *pthis, const struct metric_manifest_manager *other) {
    if (pthis->manifest_cnt != other->manifest_cnt) {
        return false;
    }

    for (size_t i = 0; i < pthis->manifest_cnt; i++) {
        if (pthis->manifests[i]->type != other->manifests[i]->type) {
            return false;
        }
        if (strcmp(pthis->manifests[i]->name, other->manifests[i]->name) != 0) {
            return false;
        }
    }

    return true;
}

void metric_manifest_free(struct metric_manifest *manifest) {
    free(manifest->name);
    free(manifest->parameters);
    free(manifest);
}

struct metric_manifest *metric_manifest_copy(const struct metric_manifest *src)
{
    struct metric_manifest *dest = (struct metric_manifest *)malloc(sizeof(struct metric_manifest));
    dest->name = strdup(src->name);
    if (src->parameters == NULL) {
        dest->parameters = NULL;
    } else {
        dest->parameters = (union metric_parameter *)malloc(sizeof(union metric_parameter));
        memcpy(dest->parameters, src->parameters, sizeof(union metric_parameter));
    }

    dest->type = src->type;
    dest->id = src->id;

    return dest;
}

struct metric_manifest_manager *metric_manifest_manager_copy(const struct metric_manifest_manager *pthis) {
    struct metric_manifest_manager *ret = metric_manifest_manager_new();
    for (size_t i = 0; i < pthis->manifest_cnt; i++) {
        struct metric_manifest *manifest = metric_manifest_copy(pthis->manifests[i]);
        metric_manifest_manager_add(ret, manifest);
    }
    return ret;
}
