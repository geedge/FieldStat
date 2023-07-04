#include <gtest/gtest.h>
#include <random>
#include <array>

#include "utils.hpp"

#include "fieldstat.h"

using namespace std;

void test_tag_query_by_comparing_with_benchmark(const struct fieldstat *instance, int field_id, Fieldstat_tag_list_wrapper **tag_list_wrapper, int tag_choice_size)
{
    struct fieldstat_tag_list *tag_list;
    size_t n_tag;
    fieldstat_get_tag_lists(instance, field_id, &tag_list, &n_tag);

    EXPECT_EQ(n_tag, tag_choice_size);

    for (int i = 0; i < tag_choice_size; i++) {
        Fieldstat_tag_list_wrapper tmp(&tag_list[i]);
        EXPECT_TRUE(tmp == *tag_list_wrapper[i]);
    }

    for (size_t i = 0; i < n_tag; i++) {
        free(tag_list[i].tag);
    }
    free(tag_list);
}

TEST(metric_test_misc, tag_serializing_given_length_1_2_or_10_for_all_types_of_metrics)
{
    struct fieldstat *instance = fieldstat_new();
    const char *metric_name[] = {"test_bytes", "test_packet_num"};
    unsigned int added_value[] = {1, 10000};
    int tag_length_choice[] = {1, 2, 10};
    int tag_length_choice_size = sizeof(tag_length_choice) / sizeof(int);
    Fieldstat_tag_list_wrapper *tags[4][tag_length_choice_size] = {NULL}; // metric type num * tag length num
    std::uniform_int_distribution<int> dist(1,100);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < tag_length_choice_size; j++)
        {
            Fieldstat_tag_list_wrapper *tmp = new Fieldstat_tag_list_wrapper(dist, tag_length_choice[i]);
            tags[i][j] = tmp;
        }
    }

    int id_counter = fieldstat_register_counter(instance, "counter");
    int id_topk = fieldstat_register_topk(instance, "topk_add", 10, metric_name, 2, 1);
    int id_hll = fieldstat_register_hll(instance, "add", 10);
    int id_histogram = fieldstat_register_histogram(instance, "add", 1, 600000, 3);
    for (int i = 0; i < tag_length_choice_size; i++) {
        fieldstat_counter_incrby(instance, id_counter, 1000, tags[0][i]->get_tag(), tags[0][i]->get_tag_count());
        fieldstat_topk_add(instance, id_topk, "test", 4, added_value, tags[1][i]->get_tag(), tags[1][i]->get_tag_count());
        test_hll_add(10, instance, id_hll, tags[2][i]->get_tag(), tags[2][i]->get_tag_count());
        fieldstat_histogram_record(instance, id_histogram, 1000, tags[3][i]->get_tag(), tags[3][i]->get_tag_count());
    }
    
    struct fieldstat *instance_from_deserialize = fieldstat_new();
    test_merge_with_blob(instance_from_deserialize, instance);

    for (int i = 0; i < tag_length_choice_size; i++) {
        test_tag_query_by_comparing_with_benchmark(instance, id_counter, tags[0], tag_length_choice_size);
        test_tag_query_by_comparing_with_benchmark(instance, id_topk, tags[1], tag_length_choice_size);
        test_tag_query_by_comparing_with_benchmark(instance, id_hll, tags[2], tag_length_choice_size);
        test_tag_query_by_comparing_with_benchmark(instance, id_histogram, tags[3], tag_length_choice_size);
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < tag_length_choice_size; j++) {
            delete tags[i][j];
        }
    }

    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

TEST(metric_test_misc, register_many_field_and_merge_by_blob)
{
    struct fieldstat *instance = fieldstat_new();
    struct fieldstat *instance_dest = fieldstat_new();
    std::function<void(const char *)> reg_funcs[] = {
        std::bind(fieldstat_register_counter, instance, std::placeholders::_1),
        std::bind(fieldstat_register_topk, instance, std::placeholders::_1, 10, (const char *[]){"test_bytes"}, 1, 0),
        std::bind(fieldstat_register_hll, instance, std::placeholders::_1, 10),
        std::bind(fieldstat_register_histogram, instance, std::placeholders::_1, 1, 600000, 3)
    };

    const string field_head[4] = {"counter", "topk", "hll", "histogram"};
    array<string, 1000> field_names;
    for (int j = 0; j < 1000; j++) { // init max field num: 256, so 1000 is large enough to test reallocating twice
        int field_type = rand() % 4;
        string tmp_rand = field_head[field_type] + to_string(j);
        field_names[j] = tmp_rand;
        reg_funcs[field_type](tmp_rand.c_str());
    }

    test_merge_with_blob(instance_dest, instance);
    int max_id = fieldstat_get_max_field_id(instance_dest);
    EXPECT_EQ(max_id, 999);
    for (int i = 0; i < 1000; i++) {
        const char *name = fieldstat_get_field_name(instance_dest, i);
        EXPECT_STREQ(name, field_names[i].c_str());
    }

    fieldstat_free(instance);
    fieldstat_free(instance_dest);
}

TEST(metric_test_misc, add_cell_with_many_tags)
{
    struct fieldstat *instance = fieldstat_new();
    const int tag_num = 100; // the average tag length is approximately 20, and the dynamic key initial length is 256, so 100 tags is large enough to test reallocating several times
    std::uniform_int_distribution<int> dist(1,100);
    Fieldstat_tag_list_wrapper *tags = new Fieldstat_tag_list_wrapper(dist, tag_num);
    int id_counter = fieldstat_register_counter(instance, "counter");
    fieldstat_counter_incrby(instance, id_counter, 1000, tags->get_tag(), tags->get_tag_count());
    fieldstat_counter_incrby(instance, id_counter, 234, tags->get_tag(), tags->get_tag_count()); // test find cell with key

    struct fieldstat *instance_from_deserialize = fieldstat_new();
    test_merge_with_blob(instance_from_deserialize, instance);

    test_tag_query_by_comparing_with_benchmark(instance, id_counter, &tags, 1);

    auto q_rets = test_get_query_result<long long>(fieldstat_counter_get, instance_from_deserialize, 0);
    EXPECT_EQ(q_rets.size(), 1);
    EXPECT_EQ(q_rets[0], 1234);

    delete tags;
    fieldstat_free(instance);
    fieldstat_free(instance_from_deserialize);
}

TEST(metric_test_misc, test_name_maping_when_merge)
{
    struct fieldstat *instance1 = fieldstat_new();
    struct fieldstat *instance2 = fieldstat_new();
    struct fieldstat *instance_summary = fieldstat_new();

    (void)fieldstat_register_counter(instance1, "counter shared");
    (void)fieldstat_register_hll(instance1, "hll1", 10);
    (void)fieldstat_register_hll(instance2, "hll2", 10);
    (void)fieldstat_register_counter(instance2, "counter shared");

    test_merge_with_blob(instance_summary, instance1);
    test_merge_with_blob(instance_summary, instance1);
    test_merge_with_blob(instance_summary, instance2);

    int field_num = fieldstat_get_max_field_id(instance_summary) + 1;
    EXPECT_EQ(field_num, 3);
    EXPECT_STREQ(fieldstat_get_field_name(instance_summary, 0), "counter shared");
    EXPECT_STREQ(fieldstat_get_field_name(instance_summary, 1), "hll1");
    EXPECT_STREQ(fieldstat_get_field_name(instance_summary, 2), "hll2");

    fieldstat_free(instance1);
    fieldstat_free(instance2);
    fieldstat_free(instance_summary);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}