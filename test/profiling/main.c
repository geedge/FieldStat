#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "fieldstat.h"
#include "tags/my_ut_hash.h"
#include "tags/my_ut_hash_inner.h"

#define ADD_OPER_NUM 5000000
// #define ADD_OPER_NUM 1
#define MAX_STRING_KEY_LEN 10

const struct fieldstat_tag TEST_TAG_INT = {"INT key_", TAG_INTEGER, {.value_longlong = 100}};


// todo: 问一下贺岚风，tag是什么样子的

int main () {
    printf("Start profiling...\n");
    clock_t start, end;
    struct fieldstat_tag TAG[4] = {
        {"INT key_", TAG_INTEGER, {.value_longlong = 100}},
        {"STRING key_", TAG_CSTRING, {.value_str = "10012312312312"}},
        {"STRING key_", TAG_CSTRING, {.value_str = "100adsffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"}},
        {"FLOAT key_", TAG_INTEGER, {.value_double = 100.0}},
    };

    struct fieldstat *instance = fieldstat_new();
    fieldstat_create_cube(instance, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 0);
    fieldstat_register_counter(instance, "counter");

    start = clock();
    // getchar();
    for (int i = 0; i < ADD_OPER_NUM; i++)
    {
        fieldstat_counter_incrby(instance, 0, 0, TAG, 4, 1);
    }
    // exit(0);
    end = clock();

    printf("Time elapsed: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
}