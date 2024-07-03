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

void fill_with_elephant_flows(Fieldstat_tag_list_wrapper *tags[], int tag_list_num)
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
        tags[i] = tmp;
    }
}

long long fuzz_fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tag_list)
{
    long long value = 0;
    int ret = fieldstat_counter_get(instance, cube_id, metric_id, tag_list, &value);
    EXPECT_EQ(ret, 0);
    return value;
}

double fuzz_fieldstat_hll_get(const struct fieldstat *instance, int cube_id, int metric_id, const struct fieldstat_tag_list *tag_list)
{
    double value = 0;
    int ret = fieldstat_hll_get(instance, cube_id, metric_id, tag_list, &value);
    EXPECT_EQ(ret, 0);
    return value;
}

TEST(Fuzz_test, many_instance_random_flow_unregister_calibrate_reset_fork_merge_comprehensive)
{
    const int METRIC_NUM = 2;
    const int METRIC_ID_COUNTER = 0;
    const int METRIC_ID_HLL = 1;
    const int CUBE_NUM = 5;
    const int INSTANCE_NUM = 10;
    const int FLOW_NUM = 5000;
    const int CELL_MAX = 5000; // must be no less than FLOW_NUM to ensure an accurate statistics
    const int TEST_ROUND = 100000;
    const int OUT_GAP = 10000;
    const char *metric_name[METRIC_NUM] = {"counter_", "hll_"};
    struct fieldstat *master = fieldstat_new();
    struct fieldstat *replica[INSTANCE_NUM];
    struct fieldstat *dest = fieldstat_new();
    
    Fieldstat_tag_list_wrapper *shared_tags[CUBE_NUM];

    // init cube
    for (int i = 0; i < CUBE_NUM; i++) {
        shared_tags[i] = new Fieldstat_tag_list_wrapper("shared_tag", i);
        int cube_id = fieldstat_create_cube(master, shared_tags[i]->get_tag(), shared_tags[i]->get_tag_count(), SAMPLING_MODE_COMPREHENSIVE, CELL_MAX);
        EXPECT_EQ(cube_id, i);
    }
    // init metric
    fieldstat_register_counter(master, metric_name[METRIC_ID_COUNTER]);
    fieldstat_register_hll(master, metric_name[METRIC_ID_HLL], 6);
    // all the possible tags
    Fieldstat_tag_list_wrapper *tag_list_wrapper[FLOW_NUM];
    fill_random_tag_of_length_1_to_3(tag_list_wrapper, FLOW_NUM);
    //all the possible operations
    long long rand_nums[TEST_ROUND];
    string *rand_strs[TEST_ROUND] = {NULL};
    for (int i = 0; i < TEST_ROUND; i++) {
        rand_nums[i] = rand() % 1000;
        rand_strs[i] = new string(string("str val") + std::to_string(rand_nums[i]));
    }
    //init instance
    for (int i = 0; i < INSTANCE_NUM; i++) {
        replica[i] = fieldstat_fork(master);
    }
    // for benchmark
    unordered_map<string, int> comp_count;
    unordered_map<string, set<string>> comp_hll;

    clock_t start = clock();
    int next_shared_tag_value = CUBE_NUM;

    for (int i = 0; i < TEST_ROUND; i++) {
        if (i != 0 && i % OUT_GAP == 0) {
            // merge
            for (int j = 0; j < INSTANCE_NUM; j++) {
                fieldstat_merge(dest, replica[j]);
            }
            for (int j = 0; j < INSTANCE_NUM; j++) {
                fieldstat_reset(replica[j]);
            }

            // modify master and calibrate
            int cube_id_to_change = rand() % CUBE_NUM;
            Fieldstat_tag_list_wrapper *new_tag = new Fieldstat_tag_list_wrapper("shared_tag", next_shared_tag_value++);
            delete shared_tags[cube_id_to_change];
            shared_tags[cube_id_to_change] = new_tag;
            fieldstat_destroy_cube(master, cube_id_to_change);
            int cube_id_new = fieldstat_create_cube(master, new_tag->get_tag(), new_tag->get_tag_count(), SAMPLING_MODE_COMPREHENSIVE, CELL_MAX);
            EXPECT_EQ(cube_id_new, cube_id_to_change); // should new the cube in the hole leaved by the destroyed cube
            // calibrate
            for (int j = 0; j < INSTANCE_NUM; j++) {
                fieldstat_calibrate(master, replica[j]);
            }
            
            // check if no merge happens in the last 100 rounds
            if (i + OUT_GAP >= TEST_ROUND) {
                break;
            }
        }
        struct fieldstat *instance = replica[rand() % INSTANCE_NUM]; // the flow randomly goes to one of the instance
        const Fieldstat_tag_list_wrapper * tag = tag_list_wrapper[rand() % FLOW_NUM];
        int cube_id = rand() % CUBE_NUM;
        const Fieldstat_tag_list_wrapper *shared_tag = shared_tags[cube_id];

        int ret_add = fieldstat_counter_incrby(instance, cube_id, METRIC_ID_COUNTER, tag->get_tag(), tag->get_tag_count(), rand_nums[i]);
        if (ret_add == FS_ERR_TOO_MANY_CELLS) {
            continue;
        }
        EXPECT_EQ(ret_add, FS_OK);
        string *val = rand_strs[i];
        ret_add = fieldstat_hll_add(instance, cube_id, METRIC_ID_HLL, tag->get_tag(), tag->get_tag_count(), val->c_str(), val->size());
        EXPECT_EQ(ret_add, FS_OK);
        string cell_key = shared_tag->to_string() + tag->to_string();
        comp_count[cell_key] += rand_nums[i];
        comp_hll[cell_key].insert(*val);
    }

    clock_t end = clock();
    printf("time: %lf\n", (double)(end - start) / CLOCKS_PER_SEC);

    for (int i = 0; i < TEST_ROUND; i++) {
        delete rand_strs[i];
    }
    for (int i = 0; i < FLOW_NUM; i++) {
        delete tag_list_wrapper[i];
    }
    for (int i = 0; i < CUBE_NUM; i++) {
        delete shared_tags[i];
    }

    long long dummy_ll;
    double dummy_d;
    int *cube_ids;
    int cube_num;
    struct fieldstat *instance_in_focus = dest;
    fieldstat_get_cubes(instance_in_focus, &cube_ids, &cube_num);
    for (int i = 0; i < cube_num; i++) {
        struct fieldstat_tag_list *shared_tag_out = fieldstat_get_shared_tags(instance_in_focus, cube_ids[i]);

        size_t cell_num0;
        struct fieldstat_tag_list *tags0;
        fieldstat_get_cells_used_by_cube(instance_in_focus, cube_ids[i], &tags0, &cell_num0);

        for (size_t j = 0; j < cell_num0; j++) {
            EXPECT_EQ(fieldstat_counter_get(instance_in_focus, cube_ids[i], METRIC_ID_COUNTER, &tags0[j], &dummy_ll), FS_OK);
            EXPECT_EQ(fieldstat_hll_get(instance_in_focus, cube_ids[i], METRIC_ID_HLL, &tags0[j], &dummy_d), FS_OK);
        }

        for (size_t j = 0; j < cell_num0; j++) {
            string tag_str_out = Fieldstat_tag_list_wrapper(&tags0[j]).to_string();
            string cell_key = Fieldstat_tag_list_wrapper(shared_tag_out).to_string() + tag_str_out;
        
            EXPECT_EQ(comp_count[cell_key], fuzz_fieldstat_counter_get(instance_in_focus, cube_ids[i], 0, &tags0[j]));
        }

        fieldstat_tag_list_arr_free(tags0, cell_num0);
        fieldstat_tag_list_arr_free(shared_tag_out, 1);
    }
    free(cube_ids);

    fieldstat_free(master);
    fieldstat_free(dest);
    for (int i = 0; i < INSTANCE_NUM; i++) {
        fieldstat_free(replica[i]);
    }
}

TEST(Fuzz_test, many_instance_random_flow_unregister_calibrate_reset_fork_merge_topk)
{
    const int CUBE_NUM = 5;
    const int INSTANCE_NUM = 10;
    const int FLOW_NUM = 50000;
    const int CELL_MAX = 50;
    const int TEST_ROUND = 100000;
    const int OUT_GAP = 10000;
    struct fieldstat *master = fieldstat_new();
    struct fieldstat *replica[INSTANCE_NUM];
    struct fieldstat *dest = fieldstat_new();
    
    Fieldstat_tag_list_wrapper *shared_tags[CUBE_NUM];

    // init cube
    for (int i = 0; i < CUBE_NUM; i++) {
        shared_tags[i] = new Fieldstat_tag_list_wrapper("shared_tag", i);
        int cube_id = fieldstat_create_cube(master, shared_tags[i]->get_tag(), shared_tags[i]->get_tag_count(), SAMPLING_MODE_TOPK, CELL_MAX);
        EXPECT_EQ(cube_id, i);
    }
    // init metric
    fieldstat_register_counter(master, "topk");
    // all the possible tags
    Fieldstat_tag_list_wrapper *tag_list_wrapper[FLOW_NUM];
    fill_with_elephant_flows(tag_list_wrapper, FLOW_NUM);
    //all the possible operations
    long long rand_nums[TEST_ROUND];
    for (int i = 0; i < TEST_ROUND; i++) {
        rand_nums[i] = rand() % 1000;
    }
    //init instance
    for (int i = 0; i < INSTANCE_NUM; i++) {
        replica[i] = fieldstat_fork(master);
    }
    // for benchmark
    unordered_map<string, unordered_map<string, int>> count_map; // hte first key is shared tag, second key is tag

    clock_t start = clock();
    int next_shared_tag_value = CUBE_NUM;

    for (int i = 0; i < TEST_ROUND; i++) {
        if (i != 0 && i % OUT_GAP == 0) {
            // merge
            for (int j = 0; j < INSTANCE_NUM; j++) {
                fieldstat_merge(dest, replica[j]);
            }
            for (int j = 0; j < INSTANCE_NUM; j++) {
                fieldstat_reset(replica[j]);
            }

            // modify master and calibrate
            int cube_id_to_change = rand() % CUBE_NUM;
            Fieldstat_tag_list_wrapper *new_tag = new Fieldstat_tag_list_wrapper("shared_tag", next_shared_tag_value++);
            delete shared_tags[cube_id_to_change];
            shared_tags[cube_id_to_change] = new_tag;
            fieldstat_destroy_cube(master, cube_id_to_change);
            int cube_id_new = fieldstat_create_cube(master, new_tag->get_tag(), new_tag->get_tag_count(), SAMPLING_MODE_TOPK, CELL_MAX);
            EXPECT_EQ(cube_id_new, cube_id_to_change); // should new the cube in the hole leaved by the destroyed cube
            // calibrate
            for (int j = 0; j < INSTANCE_NUM; j++) {
                fieldstat_calibrate(master, replica[j]);
            }
            
            // check if no merge happens in the last 100 rounds
            if (i + OUT_GAP >= TEST_ROUND) {
                break;
            }
        }
        struct fieldstat *instance = replica[rand() % INSTANCE_NUM]; // the flow randomly goes to one of the instance
        const Fieldstat_tag_list_wrapper * tag = tag_list_wrapper[rand() % FLOW_NUM];
        int cube_id = rand() % CUBE_NUM;
        const Fieldstat_tag_list_wrapper *shared_tag = shared_tags[cube_id];

        int ret_add = fieldstat_counter_incrby(instance, cube_id, 0, tag->get_tag(), tag->get_tag_count(), rand_nums[i]);
        if (ret_add == FS_ERR_TOO_MANY_CELLS) {
            continue;
        }
        EXPECT_EQ(ret_add, FS_OK);
        count_map[shared_tag->to_string()][tag->to_string()] += rand_nums[i];
    }

    clock_t end = clock();
    printf("time: %lf\n", (double)(end - start) / CLOCKS_PER_SEC);

    for (int i = 0; i < FLOW_NUM; i++) {
        delete tag_list_wrapper[i];
    }
    for (int i = 0; i < CUBE_NUM; i++) {
        delete shared_tags[i];
    }

    int *cube_ids;
    int cube_num;
    struct fieldstat *instance_in_focus = dest;
    fieldstat_get_cubes(instance_in_focus, &cube_ids, &cube_num);
    for (int i = 0; i < cube_num; i++) {
        struct fieldstat_tag_list *shared_tag_out = fieldstat_get_shared_tags(instance_in_focus, cube_ids[i]);

        size_t cell_num;
        struct fieldstat_tag_list *tags;
        fieldstat_get_cells_used_by_cube(instance_in_focus, cube_ids[i], &tags, &cell_num);

        std::vector<struct Fieldstat_tag_list_wrapper *> test_result;
        for (size_t j = 0; j < cell_num; j++) {
            test_result.push_back(new Fieldstat_tag_list_wrapper(&tags[j]));
        }

        EXPECT_GE(test_cal_topk_accuracy(test_result, count_map[Fieldstat_tag_list_wrapper(shared_tag_out).to_string()]), 0.9);

        for (size_t j = 0; j < cell_num; j++) {
            delete test_result[j];
        }

        fieldstat_tag_list_arr_free(tags, cell_num);
        fieldstat_tag_list_arr_free(shared_tag_out, 1);
    }
    free(cube_ids);

    fieldstat_free(master);
    fieldstat_free(dest);
    for (int i = 0; i < INSTANCE_NUM; i++) {
        fieldstat_free(replica[i]);
    }
}

TEST(Fuzz_test, add_and_reset_with_randomly_generated_flows_and_randomly_chosen_metric)
{
    const int FLOW_NUM = 50000;
    
    Fieldstat_tag_list_wrapper *tag_list_wrapper[FLOW_NUM];
    fill_with_elephant_flows(tag_list_wrapper, FLOW_NUM);

    struct fieldstat *instance = fieldstat_new();
    int primary_metric_id = fieldstat_register_counter(instance, "counter");
    int counter2_id = fieldstat_register_counter(instance, "counter2");
    int cube_id = fieldstat_create_cube(instance,NULL,0,SAMPLING_MODE_TOPK, 1); // K = 1, just to increase the possibility of FS_ERR_TOO_MANY_CELLS
    fieldstat_counter_incrby(instance, cube_id, primary_metric_id, tag_list_wrapper[0]->get_tag(), tag_list_wrapper[0]->get_tag_count(), 1);
    fieldstat_counter_incrby(instance, cube_id, counter2_id, tag_list_wrapper[0]->get_tag(), tag_list_wrapper[0]->get_tag_count(), 1);

    struct fieldstat_tag_list tag_list_tmp = {NULL, 0};

    for(int i = 0; i < FLOW_NUM; i++) {
        int using_id = rand() % 2 == 0 ? primary_metric_id : counter2_id;

        int ret = fieldstat_counter_incrby(instance, cube_id, using_id, tag_list_wrapper[i]->get_tag(), tag_list_wrapper[i]->get_tag_count(), 1);
        if (ret == FS_ERR_TOO_MANY_CELLS) {
            struct fieldstat_tag_list *tag_list = NULL;
            size_t n_cell = 0;
            fieldstat_get_cells_used_by_cube(instance, cube_id, &tag_list, &n_cell);
            ASSERT_EQ(n_cell, 1);

            long long value;
            tag_list_tmp.tag = (struct fieldstat_tag *)tag_list_wrapper[i]->get_tag();
            tag_list_tmp.n_tag = tag_list_wrapper[i]->get_tag_count();
            int counter_exist = fieldstat_counter_get(instance, cube_id, using_id, &tag_list_tmp, &value);
            ASSERT_EQ(counter_exist, FS_ERR_INVALID_TAG); // the tag is not added to the cube
            fieldstat_tag_list_arr_free(tag_list, n_cell);
        }

        if (i % 1000 == 0) {
            fieldstat_reset(instance);
        }
    }

    for (int i = 0; i < FLOW_NUM; i++) {
        delete tag_list_wrapper[i];
    }
    fieldstat_free(instance);
}


TEST(Fuzz_test, simple_one_for_perf)
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
        int cube_id = fieldstat_create_cube(master, shared_tags[i]->get_tag(), shared_tags[i]->get_tag_count(), SAMPLING_MODE_TOPK, CELL_MAX);
        EXPECT_EQ(cube_id, i);
    }
    // init metric
    fieldstat_register_counter(master, "topk");
    // all the possible tags
    Fieldstat_tag_list_wrapper *tag_list_wrapper[FLOW_NUM];
    fill_with_elephant_flows(tag_list_wrapper, FLOW_NUM);
    //all the possible operations
    long long *rand_nums = new long long[TEST_ROUND];
    for (int i = 0; i < TEST_ROUND; i++) {
        rand_nums[i] = rand() % 1000;
    }

    struct fieldstat *instance = master;

    clock_t start = clock();
    printf("press any key to start\n");
    getchar();

    for (int i = 0; i < TEST_ROUND; i++) {
        
        const Fieldstat_tag_list_wrapper * tag = tag_list_wrapper[rand() % FLOW_NUM];
        int cube_id = rand() % CUBE_NUM;

        (void)fieldstat_counter_incrby(instance, cube_id, 0, tag->get_tag(), tag->get_tag_count(), rand_nums[i]);
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


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
    // testing::GTEST_FLAG(filter) = "Fuzz_test.many_instance_random_flow_unregister_calibrate_reset_fork_merge_topk";
    testing::GTEST_FLAG(filter) = "-Fuzz_test.simple_one_for_perf";

	return RUN_ALL_TESTS();
}