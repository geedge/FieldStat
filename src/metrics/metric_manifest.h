#pragma once

struct para_hdr{
    long long lowest_trackable_value;
    long long highest_trackable_value;
    int significant_figures;
};

struct para_hll {
    char precision;
};

union metric_parameter {
    struct para_hdr hdr;
    struct para_hll hll;
};

struct metric_manifest {
    int id;
    char *name;
    union metric_parameter *paras;
    enum metric_type type;
};