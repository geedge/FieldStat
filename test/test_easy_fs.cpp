#include <gtest/gtest.h>
#include <stub/stub.h>
#include <fstream>
#include <thread>
#include <regex>

#include "utils.hpp"
#include "cjson/cJSON.h"

#include "fieldstat_easy.h"

#define FILENAME "./test_easy_fieldstat.json"

TEST(test_easy_fieldstat, new_and_free)
{
    struct fieldstat_easy *fse = fieldstat_easy_new(10, "instance name1231231231", &TEST_TAG_STRING, 1);
    fieldstat_easy_free(fse);
}

TEST(test_easy_fieldstat, output_to_buff)
{
    struct fieldstat_easy *fse = fieldstat_easy_new(10, NULL, &TEST_TAG_STRING, 1);
    fieldstat_easy_register_counter(fse, "metric counter");
    fieldstat_easy_counter_incrby(fse, 0, 0, &TEST_TAG_INT, 1, 1);
    // read file, should be empty
    char *buff = NULL;
    size_t buff_len = 0;
    fieldstat_easy_output(fse, &buff, &buff_len);
    cJSON *root = cJSON_Parse(buff);
    cJSON *cell = cJSON_GetArrayItem(root, 0);
    cJSON *metric = cJSON_GetObjectItem(cell, "fields");
    long long value = cJSON_GetObjectItem(metric, "metric counter")->valueint;
    EXPECT_EQ(value, 1);
    cJSON *tags = cJSON_GetObjectItem(cell, "tags");
    EXPECT_EQ(cJSON_GetObjectItem(tags, TEST_TAG_INT.key)->valueint, TEST_TAG_INT.value_longlong);
    EXPECT_STREQ(cJSON_GetObjectItem(tags, TEST_TAG_STRING.key)->valuestring, TEST_TAG_STRING.value_str);

    cJSON_Delete(root);
    free(buff);
    fieldstat_easy_free(fse);
}

cJSON *read_file()
{
    std::ifstream ifs(FILENAME);
    if (!ifs.is_open()) {
        return NULL;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    printf("content: %s\n", content.c_str());
    cJSON *root = cJSON_Parse(content.c_str());
    ifs.close();
    return root;
}

TEST(test_easy_fieldstat, output_to_file)
{
    struct fieldstat_easy *fse = fieldstat_easy_new(10, NULL, NULL, 0);
    int counter_id = fieldstat_easy_register_counter(fse, "metric counter");
    fieldstat_easy_enable_auto_output(fse, FILENAME, 1);
    sleep(3); // long enough to output
    // 1st interval: read file, should be empty
    printf("1st interval\n");
    cJSON *root = read_file();
    EXPECT_EQ(cJSON_GetArraySize(root), 0); // is empty and valid
    cJSON_Delete(root);

    fieldstat_easy_counter_incrby(fse, 0, counter_id, &TEST_TAG_INT, 1, 1);
    fieldstat_easy_counter_incrby(fse, 1, counter_id, &TEST_TAG_INT, 1, 10);
    fieldstat_easy_counter_incrby(fse, 2, counter_id, &TEST_TAG_INT, 1, 100);
    sleep(2);

    // 2nd interval: merge 3 thread's data, and output

    printf("2nd interval\n");
    root = read_file();
    EXPECT_EQ(cJSON_GetArraySize(root), 1);

    cJSON *tagged_by_int = cJSON_GetArrayItem(root, 0);
    cJSON *metric = cJSON_GetObjectItem(tagged_by_int, "fields");
    EXPECT_TRUE(metric != NULL);

    long long value = cJSON_GetObjectItem(metric, "metric counter")->valueint;
    EXPECT_EQ(value, 111);
    cJSON_Delete(root);

    // 3nd interval: no new data, just output again
    sleep(1);
    printf("3rd interval\n");
    root = read_file();
    EXPECT_EQ(cJSON_GetArraySize(root), 1);
    tagged_by_int = cJSON_GetArrayItem(root, 0);
    metric = cJSON_GetObjectItem(tagged_by_int, "fields");
    value = cJSON_GetObjectItem(metric, "metric counter")->valueint;
    EXPECT_EQ(value, 111); // should not change
    cJSON_Delete(root);

    // 4th interval: new data, output again
    fieldstat_easy_counter_incrby(fse, 0, counter_id, &TEST_TAG_DOUBLE, 1, 10086);
    sleep(2);
    printf("4th interval\n");
    root = read_file();
    cJSON *tagged_by_double = cJSON_GetArrayItem(root, 1);
    metric = cJSON_GetObjectItem(tagged_by_double, "fields");
    value = cJSON_GetObjectItem(metric, "metric counter")->valueint;
    EXPECT_EQ(value, 10086);
    cJSON_Delete(root);

    fieldstat_easy_free(fse);
    remove(FILENAME);
}

TEST(test_easy_fieldstat, empty_tag)
{
    struct fieldstat_easy *fse = fieldstat_easy_new(10, "ha", NULL, 0);
    int counter_id = fieldstat_easy_register_counter(fse, "metric counter");
    fieldstat_easy_counter_incrby(fse, 0, counter_id, NULL, 0, 1);
    fieldstat_easy_enable_auto_output(fse, FILENAME, 2);
    sleep(3); // long enough to output, but only once
    fieldstat_easy_free(fse);

    cJSON *root = read_file();
    EXPECT_EQ(cJSON_GetArraySize(root), 1);

    cJSON *tagged_by_int = cJSON_GetArrayItem(root, 0);
    cJSON *metric = cJSON_GetObjectItem(tagged_by_int, "fields");
    EXPECT_TRUE(metric != NULL);
    cJSON *metric_delta = cJSON_GetObjectItem(tagged_by_int, "fields_delta");
    EXPECT_TRUE(metric_delta != NULL);

    long long value = cJSON_GetObjectItem(metric, "metric counter")->valueint;
    EXPECT_EQ(value, 1);
    long long value_delta = cJSON_GetObjectItem(metric_delta, "metric counter")->valueint;
    EXPECT_EQ(value_delta, 1);

    cJSON_Delete(root);
    remove(FILENAME);
}

extern "C" {
    extern char *fs_easy_output_to_json(struct fieldstat_easy *fs, struct timeval *timestamp);
}

int g_output_count = 0;
char *fs_easy_output_to_json_stub(struct fieldstat_easy *fs, struct timeval *timestamp)
{
    g_output_count++;
    return NULL;
}

TEST(test_easy_fieldstat, output_interval_ok)
{
    Stub stub;
    stub.set(fs_easy_output_to_json, fs_easy_output_to_json_stub);

    struct fieldstat_easy *fse = fieldstat_easy_new(10, NULL, NULL, 0);
    fieldstat_easy_register_histogram(fse, "metric histogram", 1, 10000, 3); // a pretty time consuming metric
    fieldstat_easy_histogram_record(fse, 0, 0, &TEST_TAG_INT, 1, 1);
    fieldstat_easy_histogram_record(fse, 0, 0, &TEST_TAG_DOUBLE, 1, 10);
    fieldstat_easy_histogram_record(fse, 0, 0, &TEST_TAG_STRING, 1, 110);

    fieldstat_easy_enable_auto_output(fse, FILENAME, 1);

    sleep(30); // output 30s/1s = 20 times
    fieldstat_easy_free(fse);

    EXPECT_TRUE(g_output_count == 30 || g_output_count == 29); // missing only one is ok and expected
}

TEST(test_easy_fieldstat, ensure_data_racing_of_two_output_and_of_incyby)
{
    const int N_THREADS = 3;
    struct fieldstat_tag global_tags[1];
    struct fieldstat_tag tmptag;
    tmptag.key = "app id";
    tmptag.type = TAG_INTEGER;
    tmptag.value_longlong = 1;
    global_tags[0] = tmptag;
    
    struct fieldstat_easy *fse = fieldstat_easy_new(N_THREADS, NULL, global_tags, 1);
    int counter_id = fieldstat_easy_register_counter(fse, "incoming bytes");
    int hdr_id = fieldstat_easy_register_histogram(fse, "delay", 1, 10000, 1);
    fieldstat_easy_enable_auto_output(fse, FILENAME, 1);

    std::thread threads[N_THREADS];
    for (int thread_id = 0; thread_id < N_THREADS; thread_id++) {
        threads[thread_id] = std::thread([fse, counter_id, hdr_id, thread_id]() {
            for (int i = 0; i < 1000000; i++) { // loop million times to ensure no core dump
                fieldstat_easy_counter_incrby(fse, thread_id, counter_id, &TEST_TAG_INT, 1, 1);
                fieldstat_easy_histogram_record(fse, thread_id, hdr_id, &TEST_TAG_INT, 1, rand() % 10000);
                usleep(1); // 1us * 1000000 = 1s, just long enough to output
            }
        });
    }
    for (int i = 0; i < N_THREADS; i++) {
        threads[i].join();
    }
    fieldstat_easy_free(fse);

    remove(FILENAME);
}

TEST(test_easy_fieldstat, reset)
{
    struct fieldstat_easy *fse = fieldstat_easy_new(3, NULL, NULL, 0);
    int counter_id = fieldstat_easy_register_counter(fse, "metric counter");
    fieldstat_easy_counter_incrby(fse, 0, counter_id, &TEST_TAG_INT, 1, 1);
    fieldstat_easy_counter_incrby(fse, 1, counter_id, &TEST_TAG_INT, 1, 1);
    
    char **objects = NULL;
    size_t n_objects = 0;
    fieldstat_easy_output_array_and_reset(fse, &objects, &n_objects);
    EXPECT_EQ(n_objects, 1);
    EXPECT_TRUE(objects != NULL);
    free(objects[0]);
    free(objects);

    fieldstat_easy_output_array_and_reset(fse, &objects, &n_objects);
    EXPECT_EQ(n_objects, 0);

    fieldstat_easy_free(fse);
}

long long get_value(const char *input, const char *key) {
    cJSON *root = cJSON_Parse(input);
    cJSON *metric = cJSON_GetObjectItem(root, "fields");
    long long value = cJSON_GetObjectItem(metric, key)->valueint;
    cJSON_Delete(root);
    return value;
}

// issue：  调用环境为多线程 调用环境为多线程写 ， 没有开启后台线程输出 一个后台线程输出 。 而是使用 没有开启fieldstat的后台线程输出。而是自己启用一个线程，使用 ’fieldstat_easy_output_array‘输出，输出周期1s一次
// 输到数据库中的统计小于实际incrby的调用次数
TEST(test_easy_fieldstat, accuracy_in_multithread)
{
    const int N_THREADS = 10;
    struct fieldstat_easy *fse = fieldstat_easy_new(N_THREADS, NULL, NULL, 0);
    int counter_id = fieldstat_easy_register_counter(fse, "hit number");

    std::thread threads[N_THREADS];
    for (int thread_id = 0; thread_id < N_THREADS; thread_id++) {
        threads[thread_id] = std::thread([fse, counter_id, thread_id]() {
            for (size_t i = 0; i < 2000000ULL; i++) { // 1 million times
                fieldstat_easy_counter_incrby(fse, thread_id, counter_id, &TEST_TAG_INT, 1, 1);
            }
        });
    }

    char **objects = NULL;
    size_t n_objects = 0;
    // main thread output(every 1s)
    long long total_hit_number = 0;
    for (int i = 0; i < 10; i++) {
        fieldstat_easy_output_array_and_reset(fse, &objects, &n_objects);
        if (n_objects == 0) {
            continue;
        }

        total_hit_number += get_value(objects[0], "hit number");
        
        free(objects[0]);
        free(objects);

        sleep(1);
    }

    sleep(10); // wait for other threads to finish

    for (int i = 0; i < N_THREADS; i++) {
        threads[i].join();
    }

    EXPECT_EQ(total_hit_number, 20000000ULL); // 2 million * 10 threads

    fieldstat_easy_free(fse);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}