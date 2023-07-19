#include <stdio.h>
#include <gtest/gtest.h>
#include <functional>
#include <time.h>
#include <random>
#include "fieldstat.h"
#include "fieldstat_exporter.h"
#include "utils.hpp"

using namespace std;

void fill_random_tag_of_length_1_to_3(Fieldstat_tag_list_wrapper *tags[], int tag_list_num)
{
    std::uniform_int_distribution<int> dist(1,100);
    std::mt19937 rng();

    for (int i = 0; i < tag_list_num; i++)
    {
        Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper(dist, rand() % 3 + 1);
        tags[i] = tmp;
        tags[i]->print_tag_list();
    }
}

TEST(Fuzz_test, fuzz_test)
{
    const int METRIC_NUM = 2;
    const char *metric_name_of_topk[METRIC_NUM] = {"topk1", "topk2"};
    const char *metric_name_of_comprehensive[METRIC_NUM] = {"counter_", "hll_"};
    const int CUBE_NUM = 20;
    int cube_ids[CUBE_NUM];
    bool cube_is_topk[CUBE_NUM];
    Fieldstat_tag_list_wrapper *shared_tag[CUBE_NUM];
    for (int i = 0; i < CUBE_NUM; i++) {
        shared_tag[i] = new Fieldstat_tag_list_wrapper("shared_tag", i);
    }

    const int CELL_NUM = 200;
    Fieldstat_tag_list_wrapper *tag_list_wrapper[CELL_NUM];
    fill_random_tag_of_length_1_to_3(tag_list_wrapper, CELL_NUM);

    const int TEST_ROUND = 1000;
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance_dest = fieldstat_new();
    long long rand_nums[TEST_ROUND];
    string *rand_strs[TEST_ROUND] = {NULL};
    for (int i = 0; i < TEST_ROUND; i++) {
        rand_nums[i] = rand() % 10000;
        rand_strs[i] = new string(string("str val") + std::to_string(rand_nums[i]));
    }

    std::function<int(int)> metric_reg_funcs_comp[METRIC_NUM] = {
        std::bind(fieldstat_register_counter, instance, std::placeholders::_1, metric_name_of_comprehensive[0], false),
        std::bind(fieldstat_register_hll, instance, std::placeholders::_1, metric_name_of_comprehensive[1], 4),
    };
    std::function<int(int)> metric_reg_funcs_topk[METRIC_NUM] = {
        std::bind(fieldstat_register_counter, instance, std::placeholders::_1, metric_name_of_topk[0], false),
        std::bind(fieldstat_register_counter, instance, std::placeholders::_1, metric_name_of_topk[1], false),
    };

    std::function<int(int, int, int, int)> metric_oper_func_topk = 
        [&rand_nums, instance](int cube_id, int metric_id, int cell_id, int oper_id) {
            long long val = rand_nums[oper_id];
            return fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, val);
        };

    std::function<void(int, int, int, int)> metric_oper_func_comprehensive = 
        [&rand_nums, &rand_strs, instance](int cube_id, int metric_id, int cell_id, int oper_id) {
            if (metric_id == 0) {
                long long val = rand_nums[oper_id];
                fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, val);
            } else {
                string *val = rand_strs[oper_id];
                fieldstat_hll_add(instance, cube_id, metric_id, cell_id, val->c_str(), val->size());
            }
        };

    for (int i = 0; i < CUBE_NUM - 2; i++) { // leave 2 cube unregistered
        bool is_topk = rand() % 2 == 0;
        // bool is_topk = true;
        cube_is_topk[i] = is_topk;
        cube_ids[i] = fieldstat_register_cube(instance, shared_tag[i]->get_tag(), shared_tag[i]->get_tag_count(), is_topk ? SAMPLING_MODE_TOPK : SAMPLING_MODE_COMPREHENSIVE,
            is_topk ? 10 : CELL_NUM);
        std::function<int(int)> *metric_reg_funcs = is_topk ? metric_reg_funcs_topk : metric_reg_funcs_comp;
        for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
            metric_reg_funcs[metric_id](cube_ids[i]);
        }
    }
    cube_ids[CUBE_NUM - 2] = -1;
    cube_ids[CUBE_NUM - 1] = -1;

    clock_t start = clock();

    for (int i = 0; i < TEST_ROUND; i++) {
        if (i != 0 && i % 100 == 0) {
            char *blob;
            size_t blob_len;
            fieldstat_serialize(instance, &blob, &blob_len);
            struct fieldstat *instance_tmp = fieldstat_deserialize(blob, blob_len);
            fieldstat_merge(instance_dest, instance);
            fieldstat_free(instance_tmp);
            fieldstat_reset(instance);
            free(blob);
        }
        int cube_id = rand() % CUBE_NUM;
        if (cube_ids[cube_id] != -1) {
            if (cube_is_topk[cube_id]) {
                Fieldstat_tag_list_wrapper * tmp = tag_list_wrapper[rand() % CELL_NUM];
                int cell_id = fieldstat_cube_add(instance, cube_ids[cube_id], tmp->get_tag(), tmp->get_tag_count(), rand_nums[i]);
                if (cell_id == -1) {
                    continue;
                }
                for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
                    metric_oper_func_topk(cube_ids[cube_id], metric_id, cell_id, i);
                }
                
            } else {
                Fieldstat_tag_list_wrapper * tmp = tag_list_wrapper[rand() % CELL_NUM];
                int cell_id = fieldstat_cube_add(instance, cube_ids[cube_id], tmp->get_tag(), tmp->get_tag_count(), 1);
                if (cell_id == -1) {
                    continue;
                }
                for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
                    metric_oper_func_comprehensive(cube_ids[cube_id], metric_id, cell_id, i);
                }
            }
        } else {
            // create a new cube
            bool is_topk = rand() % 2 == 0;
            // bool is_topk = true;
            cube_is_topk[cube_id] = is_topk;
            if (is_topk) {
                cube_ids[cube_id] = fieldstat_register_cube(instance, shared_tag[cube_id]->get_tag(), shared_tag[cube_id]->get_tag_count(), SAMPLING_MODE_TOPK, 10);
                metric_reg_funcs_topk[0](cube_ids[cube_id]);
                metric_reg_funcs_topk[1](cube_ids[cube_id]);
            } else {
                cube_ids[cube_id] = fieldstat_register_cube(instance, &TEST_SHARED_TAG, 1, SAMPLING_MODE_COMPREHENSIVE, 2);
                metric_reg_funcs_comp[0](cube_ids[cube_id]);
                metric_reg_funcs_comp[1](cube_ids[cube_id]);
            }
        }
    }

    clock_t end = clock();
    printf("time: %lf\n", (double)(end - start) / CLOCKS_PER_SEC);

    for (int i = 0; i < TEST_ROUND; i++) {
        delete rand_strs[i];
    }
    for (int i = 0; i < CELL_NUM; i++) {
        delete tag_list_wrapper[i];
    }
    for (int i = 0; i < CUBE_NUM; i++) {
        delete shared_tag[i];
    }

    printf("czzzz test (dest)----------------\n");
    int *cubes;
    int cube_num;
    fieldstat_get_cubes(instance_dest, &cubes, &cube_num);
    for (int i = 0; i < cube_num; i++) {
        EXPECT_EQ(fieldstat_get_max_metric_id(instance_dest, cubes[i]), METRIC_NUM - 1);
        int *cells0;
        size_t cell_num0;
        struct fieldstat_tag_list *tags0;
        fieldstat_get_cells(instance_dest, cubes[i], 0, &cells0, &tags0, &cell_num0);
        int *cells1;
        size_t cell_num1;
        struct fieldstat_tag_list *tags1;
        fieldstat_get_cells(instance_dest, cubes[i], 1, &cells1, &tags1, &cell_num1);

        EXPECT_EQ(cell_num0, cell_num1);
        for (size_t j = 0; j < cell_num0; j++) {
            EXPECT_TRUE(Fieldstat_tag_list_wrapper(&tags0[j]) == Fieldstat_tag_list_wrapper(&tags1[j]));
            EXPECT_EQ(cells0[j], cells1[j]);
        }
        free(cells0);
        free(cells1);
        fieldstat_tag_list_arr_free(tags0, cell_num0);
        fieldstat_tag_list_arr_free(tags1, cell_num1);
    }
    free(cubes);

    printf("czzzz test (json)----------------\n");
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance_dest);
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
    printf("%s\n", json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    free(json_string);

    fieldstat_free(instance_dest);
    fieldstat_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}