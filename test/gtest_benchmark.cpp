#include <gtest/gtest.h>
#include <time.h>
#include <unordered_map>
#include <string>
#include <set>

#include "hdr/hdr_histogram.h"
#include "utils.hpp"

#include "fieldstat.h"

#define ADD_OPER_NUM 1000000
#define MAX_STRING_KEY_LEN 30

using namespace std;

static char *rand_string(char *str, size_t size)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyz.";
    if (size) {
        --size;
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof charset - 1);
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
    return str;
}

void get_rand_elephant(char buffer[MAX_STRING_KEY_LEN], int flow_nums)
{
    memset(buffer, 0, MAX_STRING_KEY_LEN);
    sprintf(buffer, "elephant%d", rand() % flow_nums);
}

void flow_id_generate(char buffer[MAX_STRING_KEY_LEN], float elephant_flow_radio, int elephant_flow_number)
{
  memset(buffer, 0, MAX_STRING_KEY_LEN);
    if ((double)rand() / (double)RAND_MAX < elephant_flow_radio) {
      get_rand_elephant(buffer, elephant_flow_number);
    } else {
      rand_string(buffer, rand() % (MAX_STRING_KEY_LEN - 1) + 2);
    }
}

class Ground_truth_topk {
   public:
    std::unordered_map<std::string, int> counts;
    std::vector<std::pair<std::string, int>> input;
    void add(std::pair<std::string, int> pair) {
      counts[pair.first] += pair.second;
    }
    void add_all(std::vector<std::pair<std::string, int>> &pairs) {
      for (size_t i = 0; i < pairs.size(); i++) {
            add(pairs[i]);
      }
    }

    void set_test_result(std::vector<std::pair<std::string, int>> &pairs) {
      input.assign(pairs.begin(), pairs.end());
    }

    double cal_accuracy() {
      std::vector<std::pair<std::string, int>> countVector(counts.begin(), counts.end());
      std::sort(countVector.begin(), countVector.end(), sortByValue);
      std::set<std::string> myset;
      int min_in_max_count = 0;
      size_t i;

      for (i = 0; i < input.size(); ++i) {
            myset.insert(countVector[i].first);
            min_in_max_count = countVector[i].second;
      }

      while (i < countVector.size()) {
            if (countVector[i].second != min_in_max_count) {
                break;
            }
            myset.insert(countVector[i].first);
            i++;
      }

      int correct = 0;
      std::set<std::string> tmpset(myset);
      for (size_t i = 0; i < input.size(); i++) {
            if (myset.find(input[i].first) != myset.end()) {
                correct++;
                tmpset.erase(input[i].first);
            }
      }

      double accuracy = (double)correct / input.size();
      return accuracy;
    }

   private:
    static bool sortByValue(const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) { return a.second > b.second; }
};

TEST(benchmark_test_performance, test_for_counter_metric)
{
    struct fieldstat *instance = fieldstat_new();
    int id_added = fieldstat_register_counter(instance, "add");

    clock_t start, end;
    start = clock();
    for (int i = 0; i < ADD_OPER_NUM; i++)
    {
        fieldstat_counter_incrby(instance, id_added, 1, &TEST_TAG_INT, 1);
    }
    end = clock();

    double duration = (double)(end - start) / CLOCKS_PER_SEC;
    printf("duration: %f\n", duration);

    EXPECT_LE(duration, ADD_OPER_NUM * 0.000001);

    fieldstat_free(instance);
}

TEST(benchmark_test_performance, test_for_topk_metric)
{
    struct fieldstat *instance = fieldstat_new();
    const char *metric_name[] = {"test_bytes", "test_packet_num", "test3", "test4"};
    unsigned int counts[] = {1, 1, 1, 1};
    int id_added = fieldstat_register_topk(instance, "add", 1000, metric_name, 4, 0); // 1000 is the biggest K in practice
    clock_t extra_start, extra_end;
    char str[MAX_STRING_KEY_LEN];
    extra_start = clock();
    for (int i = 0; i < ADD_OPER_NUM; i++) {
        flow_id_generate(str, 0.5, 10);
        (void)strlen(str);
    }
    extra_end = clock();

    clock_t start, end;
    start = clock();
    for (int i = 0; i < ADD_OPER_NUM; i++)
    {
        flow_id_generate(str, 0.5, 10);
        fieldstat_topk_add(instance, id_added, str, strlen(str), counts, &TEST_TAG_INT, 1);
    }
    end = clock();

    double duration = (double)(end - start - (extra_end - extra_start)) / CLOCKS_PER_SEC;
    printf("duration: %f\n", duration);

    EXPECT_LE(duration, ADD_OPER_NUM * 0.000001);

    fieldstat_free(instance);
}

TEST(benchmark_test_performance, test_for_hll_metric)
{
    struct fieldstat *instance = fieldstat_new();
    int id_added = fieldstat_register_hll(instance, "add", 10);

    clock_t extra_start, extra_end;
    extra_start = clock();
    for (int i = 0; i < ADD_OPER_NUM; i++) {
        std::string tmp_str = gen_rand_string(MAX_STRING_KEY_LEN);
    }
    extra_end = clock();
    clock_t start, end;
    start = clock();
    for (int i = 0; i < ADD_OPER_NUM; i++) {
        std::string tmp_str = gen_rand_string(MAX_STRING_KEY_LEN);
        fieldstat_hll_add(instance, id_added, tmp_str.c_str(), tmp_str.size(), &TEST_TAG_INT, 1);
    }
    end = clock();

    double duration = (double)(end - start - (extra_end - extra_start)) / CLOCKS_PER_SEC;
    printf("duration: %f\n", duration);

    EXPECT_LE(duration, ADD_OPER_NUM * 0.000001);

    fieldstat_free(instance);
}

TEST(benchmark_test_performance, test_for_histogram_metric)
{
    struct fieldstat *instance = fieldstat_new();
    int id_added = fieldstat_register_histogram(instance, "add", 1, 100000, 3);

    clock_t start, end;
    start = clock();
    for (int i = 0; i < ADD_OPER_NUM; i++)
    {
        fieldstat_histogram_record(instance, id_added, i, &TEST_TAG_INT, 1);
    }
    end = clock();

    double duration = (double)(end - start) / CLOCKS_PER_SEC;
    printf("duration: %f\n", duration);

    EXPECT_LE(duration, ADD_OPER_NUM * 0.000001);

    fieldstat_free(instance);
}

TEST(benchmark_test_accuracy, test_for_topk_metric)
{
    Ground_truth_topk *ground_truth = new Ground_truth_topk();
    struct fieldstat *instance = fieldstat_new();
    const char *metric_name[] = {"test_bytes", "test_prime"};
    unsigned int counts[] = {1, 2};
    int id_added = fieldstat_register_topk(instance, "add", 1000, metric_name, 2, 1); // 1000 is the biggest K in practice, and alway most difficult to keep accuracy
    vector<pair<std::string, int>> myVec;
    char buffer[MAX_STRING_KEY_LEN];
    for (int j = 0; j < ADD_OPER_NUM; j++) {
        flow_id_generate(buffer, 0.5, 1000);
        myVec.push_back(std::make_pair(buffer, rand() % 1000));
    }
    ground_truth->add_all(myVec);

    for (int i = 0; i < ADD_OPER_NUM / 2; i++) {
        fieldstat_topk_add(instance, id_added, myVec[i].first.c_str(), myVec[i].first.size(), counts, &TEST_TAG_INT, 1);
    }
    struct fieldstat *instance_dest = fieldstat_new();
    test_merge_with_blob(instance_dest, instance);
    fieldstat_reset(instance);

    for (int i = ADD_OPER_NUM / 2; i < ADD_OPER_NUM; i++) {
        fieldstat_topk_add(instance, id_added, myVec[i].first.c_str(), myVec[i].first.size(), counts, &TEST_TAG_INT, 1);
    }
    test_merge_with_blob(instance_dest, instance);

    // query and add result to ground truth
    char **key_result = NULL;
    size_t n_result = 0;
    char **metric_name_result = NULL;
    size_t n_metric = 0;
    size_t primary_key_id = 0;
    unsigned int **count_result = NULL;
    struct fieldstat_tag_list tag_list = {(struct fieldstat_tag *)&TEST_TAG_INT, 1};
    fieldstat_topk_query(instance_dest, id_added, &tag_list,
                         &key_result, &n_result, &metric_name_result, &n_metric, &primary_key_id, &count_result);
    std::vector<std::pair<std::string, int>> result_vec(n_result);
    for (size_t i = 0; i < n_result; i++) {
        result_vec[i] = std::make_pair(key_result[i], count_result[i][primary_key_id]);
    }
    ground_truth->set_test_result(result_vec);

    double acc = ground_truth->cal_accuracy();
    EXPECT_GE(acc, 0.9);
}

TEST(benchmark_test_accuracy, test_for_histogram_metric)
{
    struct fieldstat *instance = fieldstat_new();
    int id_added = fieldstat_register_histogram(instance, "add", 1, 100000, 3);
    array<int, ADD_OPER_NUM> myVec;

    for (int i = 0; i < ADD_OPER_NUM / 2; i++)
    {
        int rand_v = rand( ) % 100000;
        fieldstat_histogram_record(instance, id_added, rand_v, &TEST_TAG_INT, 1);
        myVec[i] = rand_v;
    }
    struct fieldstat *instance_dest = fieldstat_new();
    test_merge_with_blob(instance_dest, instance);
    fieldstat_reset(instance);
    for (int i = ADD_OPER_NUM / 2; i < ADD_OPER_NUM; i++)
    {
        int rand_v = rand( ) % 100000;
        fieldstat_histogram_record(instance, id_added, rand_v, &TEST_TAG_INT, 1);
        myVec[i] = rand_v;
    }
    test_merge_with_blob(instance_dest, instance);

    // ground truth
    struct hdr_histogram *histogram_standard = NULL;
    hdr_init(1, 100000, 3, &histogram_standard);
    for (size_t i = 0; i < ADD_OPER_NUM; i++) {
        hdr_record_value(histogram_standard, myVec[i]);
    }
    struct fieldstat_tag_list tag_list = {(struct fieldstat_tag *)&TEST_TAG_INT, 1};
    for (double i = 10; i <= 100; i += 10.0) {
        double standard = hdr_value_at_percentile(histogram_standard, i);
        double result = fieldstat_histogram_value_at_percentile(instance, id_added, &tag_list, i);
        EXPECT_NEAR(standard, result, standard * 0.01);
    }

    fieldstat_free(instance);
    hdr_close(histogram_standard);
}

TEST(benchmark_test_accuracy, test_for_hll_metric)
{
    struct fieldstat *instance = fieldstat_new();
    int id_added = fieldstat_register_hll(instance, "add", 10);
    vector<string> myVec;
    char buffer[3]; // smaller string so that the types of string is less

    for (int i = 0; i < ADD_OPER_NUM; i++)
    {
        rand_string(buffer, sizeof(buffer));
        fieldstat_hll_add(instance, id_added, buffer, strlen(buffer), &TEST_TAG_INT, 1);
        myVec.push_back(buffer);
    }
    struct fieldstat *instance_dest = fieldstat_new();
    test_merge_with_blob(instance_dest, instance);
    fieldstat_reset(instance);
    for (int i = ADD_OPER_NUM / 2; i < ADD_OPER_NUM; i++)
    {
        rand_string(buffer, sizeof(buffer));
        fieldstat_hll_add(instance, id_added, buffer, strlen(buffer), &TEST_TAG_INT, 1);
        myVec.push_back(buffer);
    }
    test_merge_with_blob(instance_dest, instance);

    // ground truth
    set<string> hll_standard;
    for (size_t i = 0; i < ADD_OPER_NUM; i++) {
        hll_standard.insert(myVec[i]);
    }
    struct fieldstat_tag_list tag_list = {(struct fieldstat_tag *)&TEST_TAG_INT, 1};
    double standard = (double)hll_standard.size();
    double result = fieldstat_hll_get(instance_dest, id_added, &tag_list);
    EXPECT_NEAR(standard, result, standard * 0.1);
    
    fieldstat_free(instance);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}