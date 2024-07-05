#pragma once

#include "uthash.h"
#include "fieldstat.h"

struct histogram_parameters {
    long long lowest_trackable_value;
    long long highest_trackable_value;
    int significant_figures;
};

struct hll_parameters {
    char precision;
};

union metric_parameter {
    struct histogram_parameters hdr;
    struct hll_parameters hll;
};

struct metric_manifest {
    int id;
    char *name;
    union metric_parameter *parameters;
    enum metric_type type;

    UT_hash_handle hh;
};

struct metric_manifest *metric_manifest_copy(const struct metric_manifest *src);
void metric_manifest_free(struct metric_manifest *pthis);

struct metric_manifest_manager;

struct metric_manifest_manager *metric_manifest_manager_new();
void metric_manifest_manager_free(struct metric_manifest_manager *pthis);
int metric_manifest_manager_add(struct metric_manifest_manager *pthis, struct metric_manifest *manifest);
struct metric_manifest_manager *metric_manifest_manager_copy(const struct metric_manifest_manager *pthis);

const struct metric_manifest **metric_manifest_manager_list(const struct metric_manifest_manager *pthis, size_t *n_manifest);
const struct metric_manifest*metric_manifest_manager_get_by_id(const struct metric_manifest_manager *pthis, int metric_id);
const struct metric_manifest*metric_manifest_manager_get_by_name(const struct metric_manifest_manager *pthis, const char *name);