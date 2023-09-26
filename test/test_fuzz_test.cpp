#include <stdio.h>
#include <gtest/gtest.h>
#include <functional>
#include <unordered_map>
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
        tmp->sort_tag_list();
        tags[i] = tmp;
    }
}

TEST(Fuzz_test, both_comp_and_topk_cubes_with_merge_and_reset_expecting_correct_results)
{
    const int METRIC_NUM = 2;
    const char *metric_name_of_topk[METRIC_NUM] = {"topk1", "topk2"};
    const char *metric_name_of_comprehensive[METRIC_NUM] = {"counter_", "hll_"};
    const int CELL_NUM = 50;
    const int CUBE_NUM = 10;
    int cube_ids[CUBE_NUM];
    bool cube_is_topk[CUBE_NUM];
    unordered_map<string, int> topk_count_per_metric[CUBE_NUM][METRIC_NUM];
    unordered_map<string, int> comp_count[CUBE_NUM];
    unordered_map<string, set<string>> comp_hll[CUBE_NUM];
    Fieldstat_tag_list_wrapper *shared_tag[CUBE_NUM];
    for (int i = 0; i < CUBE_NUM; i++) {
        shared_tag[i] = new Fieldstat_tag_list_wrapper("shared_tag", i);
    }

    Fieldstat_tag_list_wrapper *tag_list_wrapper[CELL_NUM];
    fill_random_tag_of_length_1_to_3(tag_list_wrapper, CELL_NUM);

    const int TEST_ROUND = 100001;
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance_dest = fieldstat_new();
    long long rand_nums[TEST_ROUND];
    string *rand_strs[TEST_ROUND] = {NULL};
    for (int i = 0; i < TEST_ROUND; i++) {
        rand_nums[i] = rand() % 10000;
        rand_strs[i] = new string(string("str val") + std::to_string(rand_nums[i]));
    }

    std::function<int(int)> metric_reg_funcs_comp[METRIC_NUM] = {
        std::bind(fieldstat_register_counter, instance, std::placeholders::_1, metric_name_of_comprehensive[0], COUNTER_MERGE_BY_SUM),
        std::bind(fieldstat_register_hll, instance, std::placeholders::_1, metric_name_of_comprehensive[1], 10), // 4: precision
    };
    std::function<int(int)> metric_reg_funcs_topk[METRIC_NUM] = {
        std::bind(fieldstat_register_counter, instance, std::placeholders::_1, metric_name_of_topk[0], COUNTER_MERGE_BY_SUM),
        std::bind(fieldstat_register_counter, instance, std::placeholders::_1, metric_name_of_topk[1], COUNTER_MERGE_BY_SUM),
    };

    for (int i = 0; i < CUBE_NUM; i++) {
        bool is_topk = rand() % 2 == 0;
        // bool is_topk = true;
        cube_is_topk[i] = is_topk;
        cube_ids[i] = fieldstat_create_cube(instance, shared_tag[i]->get_tag(), shared_tag[i]->get_tag_count(), is_topk ? SAMPLING_MODE_TOPK : SAMPLING_MODE_COMPREHENSIVE,
            is_topk ? 10000 : CELL_NUM);
        std::function<int(int)> *metric_reg_funcs = is_topk ? metric_reg_funcs_topk : metric_reg_funcs_comp;
        for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
            metric_reg_funcs[metric_id](cube_ids[i]);
        }
    }

    clock_t start = clock();

    for (int i = 0; i < TEST_ROUND; i++) {
        if (i != 0 && i % 1000 == 0) {
            char *blob;
            size_t blob_len;
            fieldstat_serialize(instance, &blob, &blob_len);
            struct fieldstat *instance_tmp = fieldstat_deserialize(blob, blob_len);
            fieldstat_merge(instance_dest, instance_tmp);
            fieldstat_free(instance_tmp);
            fieldstat_reset(instance);
            free(blob);
            
            // check if no merge happens in the last 100 rounds
            if (i + 100 >= TEST_ROUND) {
                break;
            }
        }
        int cube_id = rand() % CUBE_NUM;
        // int cube_id = 0;

        if (cube_is_topk[cube_id]) {
            const Fieldstat_tag_list_wrapper * tmp = tag_list_wrapper[rand() % CELL_NUM];
            int cell_id = fieldstat_cube_add(instance, cube_ids[cube_id], tmp->get_tag(), tmp->get_tag_count(), rand_nums[i]);
            if (cell_id == -1) {
                continue;
            }
            for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
                long long val = rand_nums[i];
                fieldstat_counter_incrby(instance, cube_id, metric_id, cell_id, val);
                topk_count_per_metric[cube_id][metric_id][tmp->to_string()] += val;
            }
        } else {
            const Fieldstat_tag_list_wrapper * tmp = tag_list_wrapper[rand() % CELL_NUM];
            int cell_id = fieldstat_cube_add(instance, cube_ids[cube_id], tmp->get_tag(), tmp->get_tag_count(), 1);
            if (cell_id == -1) {
                printf("cell_id == -1\n");
                continue;
            }
            fieldstat_counter_incrby(instance, cube_id, 0, cell_id, rand_nums[i]);
            string *val = rand_strs[i];
            fieldstat_hll_add(instance, cube_id, 1, cell_id, val->c_str(), val->size());
            string cell_key = tmp->to_string();
            comp_count[cube_id][cell_key] += rand_nums[i];
            comp_hll[cube_id][cell_key].insert(*val);
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

    int *cubes;
    int cube_num;
    struct fieldstat *instance_in_focus = instance_dest;
    fieldstat_get_cubes(instance_in_focus, &cubes, &cube_num);
    EXPECT_EQ(cube_num, CUBE_NUM);
    for (int i = 0; i < cube_num; i++) {
        EXPECT_EQ(fieldstat_get_max_metric_id(instance_in_focus, cubes[i]), METRIC_NUM - 1);
        int *cells0;
        size_t cell_num0;
        struct fieldstat_tag_list *tags0;
        fieldstat_get_cells(instance_in_focus, cubes[i], 0, &cells0, &tags0, &cell_num0);
        int *cells1;
        size_t cell_num1;
        struct fieldstat_tag_list *tags1;
        fieldstat_get_cells(instance_in_focus, cubes[i], 1, &cells1, &tags1, &cell_num1);

        EXPECT_EQ(cell_num0, cell_num1);
        for (size_t j = 0; j < cell_num0; j++) {
            EXPECT_TRUE(Fieldstat_tag_list_wrapper(&tags0[j]) == Fieldstat_tag_list_wrapper(&tags1[j]));
            EXPECT_EQ(cells0[j], cells1[j]);
        }

        if (cube_is_topk[cubes[i]]) {
            std::vector<struct Fieldstat_tag_list_wrapper *> test_result[METRIC_NUM];
            for (size_t j = 0; j < cell_num0; j++) {
                test_result[0].push_back(new Fieldstat_tag_list_wrapper(&tags0[j]));
                test_result[1].push_back(new Fieldstat_tag_list_wrapper(&tags1[j]));
            }
            EXPECT_GT(test_cal_topk_accuracy(test_result[0], topk_count_per_metric[cubes[i]][0]), 0.9);
            EXPECT_GT(test_cal_topk_accuracy(test_result[1], topk_count_per_metric[cubes[i]][1]), 0.9);

            for (size_t j = 0; j < cell_num0; j++) {
                delete test_result[0][j];
                delete test_result[1][j];
            }
        } else {
            for (size_t j = 0; j < cell_num0; j++) {
                string cell_key = Fieldstat_tag_list_wrapper(&tags0[j]).to_string();
                EXPECT_EQ(comp_count[cubes[i]][cell_key], my_fieldstat_counter_get(instance_in_focus, cubes[i], 0, cells0[j]));
                EXPECT_NEAR(comp_hll[cubes[i]][cell_key].size(), fieldstat_hll_get(instance_in_focus, cubes[i], 1, cells0[j]), comp_hll[cubes[i]][cell_key].size() * 0.2);
            }
        }

        free(cells0);
        free(cells1);
        fieldstat_tag_list_arr_free(tags0, cell_num0);
        fieldstat_tag_list_arr_free(tags1, cell_num1);
    }
    free(cubes);

    // printf("czzzz test (json)----------------\n");
    // struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance_dest);
    // char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
    // EXPECT_TRUE(json_string != NULL);
    // printf("%s\n", json_string);
    // fieldstat_json_exporter_free(fieldstat_json_exporter);
    // free(json_string);

    fieldstat_free(instance_dest);
    fieldstat_free(instance);
}


// TEST(Fuzz_test, memory_cost_test)
// {
//     const int METRIC_NUM = 10;
//     const char *metric_name_of_comprehensive[METRIC_NUM] = {"counter1", "counter2", "counter3", "counter4", "counter5", "counter6", "counter7", "counter8", "counter9", "counter10"};
//     const int CELL_NUM = 4000;
//     const int CUBE_NUM = 1;
//     int cube_ids[CUBE_NUM];

//     Fieldstat_tag_list_wrapper *shared_tag[CUBE_NUM];
//     for (int i = 0; i < CUBE_NUM; i++) {
//         shared_tag[i] = new Fieldstat_tag_list_wrapper("shared_tag", i);
//     }

//     Fieldstat_tag_list_wrapper *tag_list_wrapper[CELL_NUM];
//     fill_random_tag_of_length_1_to_3(tag_list_wrapper, CELL_NUM);

//     const int TEST_ROUND = 100000;
//     struct fieldstat *instance = fieldstat_new();
//     long long rand_nums[TEST_ROUND];
//     for (int i = 0; i < TEST_ROUND; i++) {
//         rand_nums[i] = rand() % 10000;
//     }

//     for (int i = 0; i < CUBE_NUM; i++) {
//         cube_ids[i] = fieldstat_create_cube(instance, shared_tag[i]->get_tag(), shared_tag[i]->get_tag_count(), SAMPLING_MODE_COMPREHENSIVE,
//             CELL_NUM);
//         for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
//             fieldstat_register_counter(instance, cube_ids[i], metric_name_of_comprehensive[metric_id], false);
//         }
//     }

//     for (int i = 0; i < TEST_ROUND; i++) {
//         // int cube_id = i % CUBE_NUM;
//         int cube_id = 0;
//         // int cube_id = 0;

//         const Fieldstat_tag_list_wrapper * tmp = tag_list_wrapper[rand() % CELL_NUM];
//         int cell_id = fieldstat_cube_add(instance, cube_ids[cube_id], tmp->get_tag(), tmp->get_tag_count(), 1);
//         if (cell_id == -1) {
//             continue;
//         }
//         for (int metric_id = 0; metric_id < METRIC_NUM; metric_id++) {
//             fieldstat_counter_incrby(instance, cube_ids[cube_id], metric_id, cell_id, rand_nums[i]);
//         }
//     }

//     for (int i = 0; i < CELL_NUM; i++) {
//         delete tag_list_wrapper[i];
//     }
//     for (int i = 0; i < CUBE_NUM; i++) {
//         delete shared_tag[i];
//     }
//     printf("czzzz test (json)----------------\n");
//     getchar();
//     struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new(instance);
//     char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter);
//     EXPECT_TRUE(json_string != NULL);
//     // printf("%s\n", json_string);
//     printf("export done\n");
//     getchar();
//     fieldstat_json_exporter_free(fieldstat_json_exporter);
//     free(json_string);

//     fieldstat_free(instance);
// }

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}