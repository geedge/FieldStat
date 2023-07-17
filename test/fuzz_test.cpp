
// todo: 
// cube oper: choose rand existing one, new one, delete one
// metric: randomly choose one, or gen a new one with counter type
// cell: randomly gen a tag, add, oper on this one
// serialize, merge and reset every rand times[0,100]


#include <gtest/gtest.h>
#include <functional>
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
    const int CUBE_NUM = 6;
    int cube_ids[CUBE_NUM];
    bool cube_is_topk[CUBE_NUM];
    Fieldstat_tag_list_wrapper *shared_tag[CUBE_NUM];
    fill_random_tag_of_length_1_to_3(shared_tag, CUBE_NUM);

    const int CELL_NUM = 1000;
    Fieldstat_tag_list_wrapper *tag_list_wrapper[CELL_NUM];
    fill_random_tag_of_length_1_to_3(tag_list_wrapper, CELL_NUM - 300);
    for (int i = CELL_NUM - 300; i < CELL_NUM; i++) {
        tag_list_wrapper[i] = new Fieldstat_tag_list_wrapper("elephant", i / 30);
    }

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

    for (int i = 0; i < CUBE_NUM; i++) { // leave 2 cube unregistered
        // bool is_topk = rand() % 2 == 0;
        bool is_topk = true;
        cube_is_topk[i] = is_topk;
        cube_ids[i] = fieldstat_register_cube(instance, shared_tag[i]->get_tag(), shared_tag[i]->get_tag_count(), is_topk ? SAMPLING_MODE_TOPK : SAMPLING_MODE_COMPREHENSIVE,
            is_topk ? 10 : CELL_NUM);
        std::function<int(int)> *metric_reg_funcs = is_topk ? metric_reg_funcs_topk : metric_reg_funcs_comp;
        for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
            metric_reg_funcs[metric_id](cube_ids[i]);
        }
    }
    // cube_ids[CUBE_NUM - 2] = -1;
    // cube_ids[CUBE_NUM - 1] = -1;

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
            // fixme: cube 在merge时版本控制
            // if (rand() % 100 == 0) {
            //     // delete a cube
            //     cube_ids[cube_id] = -1;
            //     fieldstat_delete_cube(instance, cube_ids[cube_id]);
            //     continue;
            // }

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
            // bool is_topk = rand() % 2 == 0;
            bool is_topk = true;
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

    for (int i = 0; i < TEST_ROUND; i++) {
        delete rand_strs[i];
    }
    for (int i = 0; i < CELL_NUM; i++) {
        delete tag_list_wrapper[i];
    }
    for (int i = 0; i < CUBE_NUM; i++) {
        delete shared_tag[i];
    }

    // struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance_dest);
    // char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
    // printf("%s\n", json_string);
    // fieldstat_json_exporter_free(fieldstat_json_exporter);
    // free(json_string);

    fieldstat_free(instance_dest);
    fieldstat_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}