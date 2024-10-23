#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils.hpp"

#include "fieldstat_easy.h"
#include "fieldstat.h"


void fill_with_elephant_flows(Fieldstat_tag_list_wrapper *fields[], int tag_list_num)
{
    for (int i = 0; i < tag_list_num; i++)
    {
        Fieldstat_tag_list_wrapper *tmp;
        int rand_ret = rand() % 10;
        if (rand_ret < 5) {
            tmp = new Fieldstat_tag_list_wrapper("elephant", rand() % 50); // most hit
        } else if (rand_ret == 6 || rand_ret == 7) {
            tmp = new Fieldstat_tag_list_wrapper("mid", rand() % 200);
        } else {
            tmp = new Fieldstat_tag_list_wrapper("mouse", rand() % 10000);
        }
        fields[i] = tmp;
    }
}


TEST(perf, simple_one_for_perf_topk)
{
    const int CUBE_NUM = 5;
    const int FLOW_NUM = 50000;
    const int CELL_MAX = 50;
    const int TEST_ROUND = 500000;
    struct fieldstat *master = fieldstat_new();
    
    Fieldstat_tag_list_wrapper *shared_tags[CUBE_NUM];

    // init cube
    for (int i = 0; i < CUBE_NUM; i++) {
        shared_tags[i] = new Fieldstat_tag_list_wrapper("shared_tag", i);
        int cube_id = fieldstat_cube_create(master, shared_tags[i]->get_tag(), shared_tags[i]->get_tag_count());
        EXPECT_EQ(cube_id, i);
        fieldstat_register_counter(master, cube_id, "topk");
        fieldstat_cube_set_sampling(master, cube_id, SAMPLING_MODE_TOPK, CELL_MAX, 0);
    }
    // init metric
    
    // all the possible fields
    Fieldstat_tag_list_wrapper *tag_list_wrapper[FLOW_NUM];
    fill_with_elephant_flows(tag_list_wrapper, FLOW_NUM);
    //all the possible operations
    long long *rand_nums = new long long[TEST_ROUND];
    for (int i = 0; i < TEST_ROUND; i++) {
        rand_nums[i] = rand() % 1000;
    }

    struct fieldstat *instance = master;

    clock_t start = clock();
    printf("press any key to start v46\n");
    getchar();

    for (int i = 0; i < TEST_ROUND; i++) {
        
        const Fieldstat_tag_list_wrapper * field = tag_list_wrapper[rand() % FLOW_NUM];
        int cube_id = rand() % CUBE_NUM;

        (void)fieldstat_counter_incrby(instance, cube_id, 0, field->get_tag(), field->get_tag_count(), rand_nums[i]);
    }

    clock_t end = clock();
    printf("time: %lf\n", (double)(end - start) / CLOCKS_PER_SEC);

    for (int i = 0; i < FLOW_NUM; i++) {
        delete tag_list_wrapper[i];
    }
    for (int i = 0; i < CUBE_NUM; i++) {
        delete shared_tags[i];
    }
    delete[] rand_nums;
    fieldstat_free(master);
}

TEST(perf, simple_one_for_perf_spreadsketch)
{
    const int CELL_MAX = 100;
    const int TEST_ROUND = 500000;
    struct fieldstat *instance = fieldstat_new();
    
    int cube_id = fieldstat_cube_create(instance, &TEST_FIELD_STRING, 1);
    fieldstat_register_hll(instance, cube_id, "hll", 6);
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_TOP_CARDINALITY, CELL_MAX, 0);

    SpreadSketchZipfGenerator generator(1.0, CELL_MAX * 10);
    Fieldstat_tag_list_wrapper *cell_dimension[TEST_ROUND];
    Fieldstat_tag_list_wrapper *items[TEST_ROUND];
    for (int i = 0; i < TEST_ROUND; i++) {
        Flow flow = generator.next();
        cell_dimension[i] = new Fieldstat_tag_list_wrapper("src_ip", flow.src_ip.c_str());
        items[i] = new Fieldstat_tag_list_wrapper("dst_ip", flow.dst_ip.c_str());
    }

    clock_t start = clock();
    printf("press any key to start \n");
    getchar();

    for (int i = 0; i < TEST_ROUND; i++) {
        fieldstat_hll_add_fields(instance, cube_id, 0, cell_dimension[i]->get_tag(), cell_dimension[i]->get_tag_count(), items[i]->get_field_ptr_array(), items[i]->get_tag_count());
    }

    clock_t end = clock();
    printf("time: %lf second\n", (double)(end - start) / CLOCKS_PER_SEC);

    fieldstat_free(instance);
}

void generate_random_string(char *str, size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < length - 1; i++) {
        int key = rand() % (int)(sizeof(charset) - 1);
        str[i] = charset[key];
    }
    str[length - 1] = '\0';
}

long long generate_random_integer() {
    return rand();
}

struct field *generate_random_dimensions(enum field_type type, int n_dimensions) {
    struct field *dimensions = (struct field *)malloc(sizeof(struct field) * n_dimensions);
    char key[16];
    int max_length = 16;

    for (int i = 0; i < n_dimensions; i++) {
        
        if (type == FIELD_VALUE_CSTRING) {
            snprintf(key, sizeof(key), "str_%d", i + 1);
            dimensions[i].type = FIELD_VALUE_CSTRING;
            char *random_str = (char *)malloc(max_length);

            generate_random_string(random_str, max_length);
            dimensions[i].value_str = random_str;
        } else if (type == FIELD_VALUE_INTEGER) {
            snprintf(key, sizeof(key), "int_%d", i + 1);
            dimensions[i].type = FIELD_VALUE_INTEGER;
            dimensions[i].value_longlong = rand() % 100; // 随机整数
        }
        dimensions[i].key = strdup(key);
    }

    return dimensions;
}

void free_dimensions(struct field *dimensions, int n_dimensions) {
    for (int i = 0; i < n_dimensions; i++) {
        if (dimensions[i].type == FIELD_VALUE_CSTRING) {
            free((void *)dimensions[i].value_str);
        }
        free((void *)dimensions[i].key);
    }
    free(dimensions);
}

// 初始化fieldstat_easy实例
struct fieldstat_easy *initialize_fieldstat_easy(const char *name, const struct field *global_dimensions, size_t n_global_dimensions, int num_counters, int *counter_ids) {
    struct fieldstat_easy *fse = fieldstat_easy_new(1, name, global_dimensions, n_global_dimensions);

    // 注册计数器
    for (int i = 0; i < num_counters; i++) {
        char metric_name[64];
        snprintf(metric_name, sizeof(metric_name), "counter_%d", i);
        int counter_id = fieldstat_easy_register_counter(fse, metric_name);
        if (counter_id < 0) {
            fprintf(stderr, "Failed to register counter %s\n", metric_name);
            exit(EXIT_FAILURE);
        }
        counter_ids[i] = counter_id;
    }
    return fse;
}

struct fs_instance {
    struct fieldstat *instance;
    struct fieldstat_easy *instance_easy;
};

clock_t g_run_time = 0;
void tested_function(struct fs_instance *instance, int cube_id, int metric_id, const struct field *dimensions, int n_dimensions, long long count) {
    clock_t start = clock();

    if (instance->instance != NULL) {
        fieldstat_counter_incrby(instance->instance, cube_id, metric_id, dimensions, n_dimensions, count);
    } else {
        fieldstat_easy_counter_incrby(instance->instance_easy, 0, metric_id, dimensions, n_dimensions, count);
    }

    clock_t end = clock();
    g_run_time += end - start;
}

void tested_function_batch(struct fs_instance *instance, int cube_id, int metric_ids[], const struct field *dimensions, int n_dimensions, long long counts[], size_t n_metrics) {
    clock_t start = clock();

    if (instance->instance != NULL) {
        fieldstat_counter_incrby_batch(instance->instance, cube_id,  dimensions, n_dimensions, metric_ids, counts, n_metrics);
    } else {
        fieldstat_easy_counter_incrby_batch(instance->instance_easy, 0, metric_ids, dimensions, n_dimensions, counts, n_metrics);
    }

    clock_t end = clock();
    g_run_time += end - start;
}

TEST(perf, complex_instance_group_for_real_case) {
    const int N_LOOP = 50000;

    printf("press any key to start\n");
    getchar();


    // traffic_general_stat
    {
        struct field *global_int_dimension = generate_random_dimensions(FIELD_VALUE_INTEGER, 1);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);
        struct field global_dimensions[4];
        memcpy(&global_dimensions[0], &global_int_dimension[0], sizeof(struct field));
        memcpy(&global_dimensions[1], &global_str_dimensions[0], sizeof(struct field) * 3);

        int num_counters = 13;
        int counter_ids[13];
        struct fieldstat_easy *fse_traffic_general = initialize_fieldstat_easy("traffic_general_stat", global_dimensions, 4, num_counters, counter_ids);
        struct fs_instance instance;
        instance.instance_easy = fse_traffic_general;
        instance.instance = NULL;

        // 在循环中调用counter_incrby
        long long count = rand() % 10000 + 1;
        for (int counter_index = 0; counter_index < num_counters; counter_index++) {
            int metric_id = counter_ids[counter_index];
            for (int iter = 0; iter < N_LOOP; iter++) {
                tested_function(&instance, 0, metric_id, NULL, 0, count);
            }
        }

        fieldstat_easy_free(fse_traffic_general);
        free_dimensions(global_int_dimension, 1);
        free_dimensions(global_str_dimensions, 3);
    }

    // traffic_application_protocol_stat
    {
        struct field *global_int_dimension = generate_random_dimensions(FIELD_VALUE_INTEGER, 1);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);

        // 合并global_dimensions
        struct field global_dimensions[4];
        memcpy(&global_dimensions[0], &global_int_dimension[0], sizeof(struct field));
        memcpy(&global_dimensions[1], &global_str_dimensions[0], sizeof(struct field) * 3);

        int num_counters = 19;
        int counter_ids[19];
        struct fieldstat_easy *fse_traffic_app = initialize_fieldstat_easy("traffic_application_protocol_stat", global_dimensions, 4, num_counters, counter_ids);
        struct fs_instance instance;
        instance.instance_easy = fse_traffic_app;
        instance.instance = NULL;

        // dimensions: string :2
        int n_dimensions = 2;
        struct field *dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, n_dimensions);

        long long count = rand() % 10000 + 1;
        for (int counter_index = 0; counter_index < num_counters; counter_index++) {
            int metric_id = counter_ids[counter_index];
            for (int iter = 0; iter < N_LOOP; iter++) {
                // 随机产生count
                tested_function(&instance, 0, metric_id, dimensions, n_dimensions, count);
            }
        }
        free_dimensions(dimensions, n_dimensions);

        fieldstat_easy_free(fse_traffic_app);
        free_dimensions(global_int_dimension, 1);
        free_dimensions(global_str_dimensions, 3);
    }

    // TRAFFIC-SKETCH-METRIC
    {
        struct field *global_int_dimensions = generate_random_dimensions(FIELD_VALUE_INTEGER, 2);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);

        // 合并global_dimensions
        struct field global_dimensions[5];
        memcpy(&global_dimensions[0], &global_int_dimensions[0], sizeof(struct field) * 2);
        memcpy(&global_dimensions[2], &global_str_dimensions[0], sizeof(struct field) * 3);

        int num_counters = 19;
        int counter_ids[19];
        struct fieldstat_easy *fse_traffic_sketch = initialize_fieldstat_easy("TRAFFIC-SKETCH-METRIC", global_dimensions, 5, num_counters, counter_ids);
        struct fs_instance instance;
        instance.instance_easy = fse_traffic_sketch;
        instance.instance = NULL;

        // dimension: string: [6,16]
        int n_dimensions = 16;

        // 在循环中调用counter_incrby
        long long count = rand() % 10000 + 1;
        int N_ITER_PER_COUNTER = N_LOOP / num_counters;
        for (int counter_index = 0; counter_index < num_counters; counter_index++) {
            int metric_id = counter_ids[counter_index];
            for (int iter = 0; iter < N_ITER_PER_COUNTER; iter++) {
                // 随机生成dimensions的值
                struct field *dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, n_dimensions);

                // 调用counter_incrby
                tested_function(&instance, 0, metric_id, dimensions, n_dimensions, count);

                // 释放dimensions
                free_dimensions(dimensions, n_dimensions);
            }
        }

        free_dimensions(global_int_dimensions, 2);
        free_dimensions(global_str_dimensions, 3);
        fieldstat_easy_free(fse_traffic_sketch);
    }

    // TOPK-METRIC
    {
        struct fieldstat *fs_topk = fieldstat_new();
        struct fs_instance instance;
        instance.instance_easy = NULL;
        instance.instance = fs_topk;

        // global_dimensions: integer :1, string:3
        struct field *global_int_dimension = generate_random_dimensions(FIELD_VALUE_INTEGER, 1);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);
        struct field global_dimensions[4];
        memcpy(&global_dimensions[0], &global_int_dimension[0], sizeof(struct field));
        memcpy(&global_dimensions[1], &global_str_dimensions[0], sizeof(struct field) * 3);

        // 注册cube
        int cube_ids[24];
        int num_cubes = 24;
        for (int i = 0; i < num_cubes; i++) {
            struct field cube_dimension;
            char cube_key[16];
            snprintf(cube_key, sizeof(cube_key), "cube_%d", i + 1);
            cube_dimension.key = cube_key;
            cube_dimension.type = FIELD_VALUE_INTEGER;
            cube_dimension.value_longlong = i + 1;

            int cube_id = fieldstat_cube_create(fs_topk, &cube_dimension, 1);
            if (cube_id < 0) {
                fprintf(stderr, "Failed to create cube %d\n", i);
                exit(EXIT_FAILURE);
            }
            cube_ids[i] = cube_id;
        }

        // 注册metrics
        int metric_ids[7];
        int num_metrics = 7;
        for (int j = 0; j < num_metrics; j++) {
            char metric_name[64];
            snprintf(metric_name, sizeof(metric_name), "counter_%d", j);

            for (int i = 0; i < num_cubes; i++) {
                int cube_id = cube_ids[i];
                int metric_id = fieldstat_register_counter(fs_topk, cube_id, metric_name);
                metric_ids[j] = metric_id;
            }
        }

        // 设置sampling
        for (int i = 0; i < num_cubes; i++) {
            fieldstat_cube_set_sampling(fs_topk, cube_ids[i], SAMPLING_MODE_TOPK, 1000, rand() % num_metrics);
        }

        // 在循环中调用counter_incrby
        long long count = rand() % 10000 + 1;
        for (int iter = 0; iter < N_LOOP; iter++) {
            struct field *cell_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 2); // dimensions: string:2
            for (int cube_index = 0; cube_index < num_cubes; cube_index++) {
                int cube_id = cube_ids[cube_index];
                for (int metric_index = 0; metric_index < num_metrics; metric_index++) {
                    int metric_id = metric_ids[metric_index];
                    
                    tested_function(&instance, cube_id, metric_id, cell_dimensions, 2, count);
                    
                }
            }
            free_dimensions(cell_dimensions, 2);
        }

        fieldstat_free(fs_topk);
        free_dimensions(global_int_dimension, 1);
        free_dimensions(global_str_dimensions, 3);
    }

    // DOS-SKETCH-METRIC
    {
        struct fieldstat *fs_dos = fieldstat_new();
        struct fs_instance instance;
        instance.instance_easy = NULL;
        instance.instance = fs_dos;

        // global_dimensions: integer:2, string:3
        struct field *global_int_dimensions = generate_random_dimensions(FIELD_VALUE_INTEGER, 2);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);

        // 合并global_dimensions
        struct field global_dimensions[5];
        memcpy(&global_dimensions[0], &global_int_dimensions[0], sizeof(struct field) * 2);
        memcpy(&global_dimensions[2], &global_str_dimensions[0], sizeof(struct field) * 3);

        // 创建cube
        int cube_ids[2];
        int num_cubes = 2;
        for (int i = 0; i < num_cubes; i++) {
            struct field cube_dimension;
            char cube_key[16];
            snprintf(cube_key, sizeof(cube_key), "cube_%d", i + 1);
            cube_dimension.key = cube_key;
            cube_dimension.type = FIELD_VALUE_INTEGER;
            cube_dimension.value_longlong = i + 1;

            int cube_id = fieldstat_cube_create(fs_dos, &cube_dimension, 1);
            if (cube_id < 0) {
                fprintf(stderr, "Failed to create cube %d\n", i);
                exit(EXIT_FAILURE);
            }
            cube_ids[i] = cube_id;
        }

        // 注册metrics
        int num_metrics = 3;
        for (int i = 0; i < num_cubes; i++) {
            int cube_id = cube_ids[i];
            for (int j = 0; j < num_metrics; j++) {
                char metric_name[64];
                snprintf(metric_name, sizeof(metric_name), "counter_%d", j);
                int metric_id = fieldstat_register_counter(fs_dos, cube_id, metric_name);
                if (metric_id < 0) {
                    fprintf(stderr, "Failed to register metric %s for cube %d\n", metric_name, cube_id);
                    exit(EXIT_FAILURE);
                }
            }
        }

        // 设置sampling
        for (int i = 0; i < num_cubes; i++) {
            fieldstat_cube_set_sampling(fs_dos, cube_ids[i], SAMPLING_MODE_TOPK, 1000, rand() % num_metrics);
        }

        // 在循环中调用counter_incrby
        int n_dimensions = 5; // dimension: string:  [2,5]

        for (int iter = 0; iter < N_LOOP; iter++) {
            struct field *cell_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, n_dimensions);
            for (int cube_index = 0; cube_index < num_cubes; cube_index++) {
                int cube_id = cube_ids[cube_index];
                for (int metric_index = 0; metric_index < num_metrics; metric_index++) {
                    int metric_id = metric_index;
    
                    long long count = rand() % 10000 + 1;

                    tested_function(&instance, cube_id, metric_id, cell_dimensions, n_dimensions, count);
                }
            }
            free_dimensions(cell_dimensions, n_dimensions);
        }
        free_dimensions(global_int_dimensions, 2);
        free_dimensions(global_str_dimensions, 3);
        fieldstat_free(fs_dos);
    }

    printf("run (%d) loops in %lf seconds\n", N_LOOP, (double)(g_run_time) / CLOCKS_PER_SEC);
}


TEST(perf, complex_instance_group_for_real_case_use_batch) {
    const int N_LOOP = 50000;

    printf("press any key to start\n");
    getchar();

    // traffic_general_stat
    {
        struct field *global_int_dimension = generate_random_dimensions(FIELD_VALUE_INTEGER, 1);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);
        struct field global_dimensions[4];
        memcpy(&global_dimensions[0], &global_int_dimension[0], sizeof(struct field));
        memcpy(&global_dimensions[1], &global_str_dimensions[0], sizeof(struct field) * 3);

        int num_counters = 13;
        int counter_ids[13];
        struct fieldstat_easy *fse_traffic_general = initialize_fieldstat_easy("traffic_general_stat", global_dimensions, 4, num_counters, counter_ids);
        struct fs_instance instance;
        instance.instance_easy = fse_traffic_general;
        instance.instance = NULL;

        long long counts[13];
        for (int i = 0; i < 13; i++) {
            counts[i] = rand() % 10000 + 1;
        }
        for (int iter = 0; iter < N_LOOP; iter++) {
            tested_function_batch(&instance, 0, counter_ids, NULL, 0, counts, 13);
        }

        fieldstat_easy_free(fse_traffic_general);
        free_dimensions(global_int_dimension, 1);
        free_dimensions(global_str_dimensions, 3);
    }

    // traffic_application_protocol_stat
    {
        struct field *global_int_dimension = generate_random_dimensions(FIELD_VALUE_INTEGER, 1);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);

        // 合并global_dimensions
        struct field global_dimensions[4];
        memcpy(&global_dimensions[0], &global_int_dimension[0], sizeof(struct field));
        memcpy(&global_dimensions[1], &global_str_dimensions[0], sizeof(struct field) * 3);

        int num_counters = 19;
        int counter_ids[19];
        struct fieldstat_easy *fse_traffic_app = initialize_fieldstat_easy("traffic_application_protocol_stat", global_dimensions, 4, num_counters, counter_ids);
        struct fs_instance instance;
        instance.instance_easy = fse_traffic_app;
        instance.instance = NULL;

        // dimensions: string :2
        int n_dimensions = 2;
        struct field *dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, n_dimensions);

        long long counts[19];
        for (int i = 0; i < 19; i++) {
            counts[i] = rand() % 10000 + 1;
        }
        for (int iter = 0; iter < N_LOOP; iter++) {
            tested_function_batch(&instance, 0, counter_ids, dimensions, n_dimensions, counts, 19);
        }
        free_dimensions(dimensions, n_dimensions);

        fieldstat_easy_free(fse_traffic_app);
        free_dimensions(global_int_dimension, 1);
        free_dimensions(global_str_dimensions, 3);
    }

    // TRAFFIC-SKETCH-METRIC
    {
        struct field *global_int_dimensions = generate_random_dimensions(FIELD_VALUE_INTEGER, 2);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);

        // 合并global_dimensions
        struct field global_dimensions[5];
        memcpy(&global_dimensions[0], &global_int_dimensions[0], sizeof(struct field) * 2);
        memcpy(&global_dimensions[2], &global_str_dimensions[0], sizeof(struct field) * 3);

        int num_counters = 19;
        int counter_ids[19];
        struct fieldstat_easy *fse_traffic_sketch = initialize_fieldstat_easy("TRAFFIC-SKETCH-METRIC", global_dimensions, 5, num_counters, counter_ids);
        struct fs_instance instance;
        instance.instance_easy = fse_traffic_sketch;
        instance.instance = NULL;

        long long counts[num_counters];
        for (int i = 0; i < num_counters; i++) {
            counts[i] = rand() % 10000 + 1;
        }
        for (int iter = 0; iter < N_LOOP; iter++) {
            struct field *cell_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 16); // dimensions: string:16
            tested_function_batch(&instance, 0, counter_ids, cell_dimensions, 16, counts, num_counters);
            free_dimensions(cell_dimensions, 16);
        }

        free_dimensions(global_int_dimensions, 2);
        free_dimensions(global_str_dimensions, 3);
        fieldstat_easy_free(fse_traffic_sketch);
    }

    // TOPK-METRIC
    {
        struct fieldstat *fs_topk = fieldstat_new();
        struct fs_instance instance;
        instance.instance_easy = NULL;
        instance.instance = fs_topk;

        // global_dimensions: integer :1, string:3
        struct field *global_int_dimension = generate_random_dimensions(FIELD_VALUE_INTEGER, 1);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);
        struct field global_dimensions[4];
        memcpy(&global_dimensions[0], &global_int_dimension[0], sizeof(struct field));
        memcpy(&global_dimensions[1], &global_str_dimensions[0], sizeof(struct field) * 3);

        // 注册cube
        int cube_ids[24];
        int num_cubes = 24;
        for (int i = 0; i < num_cubes; i++) {
            struct field cube_dimension;
            char cube_key[16];
            snprintf(cube_key, sizeof(cube_key), "cube_%d", i + 1);
            cube_dimension.key = cube_key;
            cube_dimension.type = FIELD_VALUE_INTEGER;
            cube_dimension.value_longlong = i + 1;

            int cube_id = fieldstat_cube_create(fs_topk, &cube_dimension, 1);
            if (cube_id < 0) {
                fprintf(stderr, "Failed to create cube %d\n", i);
                exit(EXIT_FAILURE);
            }
            cube_ids[i] = cube_id;
        }

        // 注册metrics
        int metric_ids[7];
        int num_metrics = 7;
        for (int j = 0; j < num_metrics; j++) {
            char metric_name[64];
            snprintf(metric_name, sizeof(metric_name), "counter_%d", j);

            for (int i = 0; i < num_cubes; i++) {
                int cube_id = cube_ids[i];
                int metric_id = fieldstat_register_counter(fs_topk, cube_id, metric_name);
                metric_ids[j] = metric_id;
            }
        }

        // 设置sampling
        for (int i = 0; i < num_cubes; i++) {
            fieldstat_cube_set_sampling(fs_topk, cube_ids[i], SAMPLING_MODE_TOPK, 1000, rand() % num_metrics);
        }

        // 在循环中调用counter_incrby
        long long counts[7];
        for (int i = 0; i < 7; i++) {
            counts[i] = rand() % 10000 + 1;
        }
        for (int iter = 0; iter < N_LOOP; iter++) {
            struct field *cell_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 2); // dimensions: string:2
            for (int cube_index = 0; cube_index < num_cubes; cube_index++) {
                int cube_id = cube_ids[cube_index];
                
                tested_function_batch(&instance, cube_id, metric_ids, cell_dimensions, 2, counts, 7);
            }
            free_dimensions(cell_dimensions, 2);
        }

        fieldstat_free(fs_topk);
        free_dimensions(global_int_dimension, 1);
        free_dimensions(global_str_dimensions, 3);
    }

    // DOS-SKETCH-METRIC
    {
        struct fieldstat *fs_dos = fieldstat_new();
        struct fs_instance instance;
        instance.instance_easy = NULL;
        instance.instance = fs_dos;

        // global_dimensions: integer:2, string:3
        struct field *global_int_dimensions = generate_random_dimensions(FIELD_VALUE_INTEGER, 2);
        struct field *global_str_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, 3);

        // 合并global_dimensions
        struct field global_dimensions[5];
        memcpy(&global_dimensions[0], &global_int_dimensions[0], sizeof(struct field) * 2);
        memcpy(&global_dimensions[2], &global_str_dimensions[0], sizeof(struct field) * 3);

        // 创建cube
        int cube_ids[2];
        int num_cubes = 2;
        for (int i = 0; i < num_cubes; i++) {
            struct field cube_dimension;
            char cube_key[16];
            snprintf(cube_key, sizeof(cube_key), "cube_%d", i + 1);
            cube_dimension.key = cube_key;
            cube_dimension.type = FIELD_VALUE_INTEGER;
            cube_dimension.value_longlong = i + 1;

            int cube_id = fieldstat_cube_create(fs_dos, &cube_dimension, 1);
            if (cube_id < 0) {
                fprintf(stderr, "Failed to create cube %d\n", i);
                exit(EXIT_FAILURE);
            }
            cube_ids[i] = cube_id;
        }

        // 注册metrics
        int num_metrics = 3;
        int metric_ids[3];  
        for (int i = 0; i < num_cubes; i++) {
            int cube_id = cube_ids[i];
            for (int j = 0; j < num_metrics; j++) {
                char metric_name[64];
                snprintf(metric_name, sizeof(metric_name), "counter_%d", j);
                int metric_id = fieldstat_register_counter(fs_dos, cube_id, metric_name);
                if (metric_id < 0) {
                    fprintf(stderr, "Failed to register metric %s for cube %d\n", metric_name, cube_id);
                    exit(EXIT_FAILURE);
                }
                metric_ids[j] = metric_id;
            }
        }

        // 设置sampling
        for (int i = 0; i < num_cubes; i++) {
            fieldstat_cube_set_sampling(fs_dos, cube_ids[i], SAMPLING_MODE_TOPK, 1000, rand() % num_metrics);
        }

        // 在循环中调用counter_incrby
        int n_dimensions = 5; // dimension: string:  [2,5]
        long long counts[3];
        for (int i = 0; i < 3; i++) {
            counts[i] = rand() % 10000 + 1;
        }

        for (int iter = 0; iter < N_LOOP; iter++) {
            struct field *cell_dimensions = generate_random_dimensions(FIELD_VALUE_CSTRING, n_dimensions);
            for (int cube_index = 0; cube_index < num_cubes; cube_index++) {
                int cube_id = cube_ids[cube_index];
                tested_function_batch(&instance, cube_id, metric_ids, cell_dimensions, n_dimensions, counts, 3);
            }
            free_dimensions(cell_dimensions, n_dimensions);
        }
        free_dimensions(global_int_dimensions, 2);
        free_dimensions(global_str_dimensions, 3);
        fieldstat_free(fs_dos);
    }

    printf("run (%d) loops in %lf seconds\n", N_LOOP, (double)(g_run_time) / CLOCKS_PER_SEC);
}

#include "uthash.h" 
struct my_struct {
    int id;                    /* we'll use this field as the key */
    UT_hash_handle hh;         /* makes this structure hashable */
};
TEST(perf, what_will_uthash_do) {
    int loop = 1000;
    struct my_struct *users = NULL;
    struct my_struct *s;
    for (int i = 0; i < loop; i++) {
        s = (struct my_struct*)malloc(sizeof(struct my_struct));
        s->id = i;
        HASH_ADD_INT( users, id, s );
    }

    // 得到内部每个bucket 里的链长度。
    std::vector<int> item_in_bucket;
    
    for (unsigned i = 0; i < users->hh.tbl->num_buckets; i++) {
        int count = 0;

        struct UT_hash_table *tbl = users->hh.tbl;
        struct UT_hash_handle *hh = tbl->buckets[i].hh_head;
        while (hh != NULL) {
            count++;
            hh = hh->hh_next;
        }

        item_in_bucket.push_back(count);
    }

    for (int i = 0; i < item_in_bucket.size(); i++) {
        printf("bucket %d has %d items\n", i, item_in_bucket[i]);
    }
    printf("mean of items in bucket: %lf\n", (double)loop / item_in_bucket.size());
    double std_dev = 0;
    for (int i = 0; i < item_in_bucket.size(); i++) {
        std_dev += (item_in_bucket[i] - loop / item_in_bucket.size()) * (item_in_bucket[i] - loop / item_in_bucket.size());
    }
    std_dev = sqrt(std_dev / item_in_bucket.size());
    printf("std_dev of items in bucket: %lf\n", std_dev);
    int number_of_empty_bucket = 0;
    for (int i = 0; i < item_in_bucket.size(); i++) {
        if (item_in_bucket[i] == 0) {
            number_of_empty_bucket++;
        }
    }
    printf("number of empty bucket: %d\n", number_of_empty_bucket);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    testing::GTEST_FLAG(filter) = "*complex_instance_group_for_real_case";
    // testing::GTEST_FLAG(filter) = "*what_will_uthash_do";

    return RUN_ALL_TESTS();
}