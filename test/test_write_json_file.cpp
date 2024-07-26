#include <gtest/gtest.h>

#include "utils.hpp"
#include "cjson/cJSON.h"

#include "fieldstat.h"
#include "fieldstat_exporter.h"
#include "fieldstat_easy.h"

const int N_THREAD = 2;

// static struct fieldstat_easy *get_hll_fieldstat_easy()
// {
//     /*
//         fieldstat easy does not support hll
//     */
//     assert(0);
//     return NULL;
// }

extern "C" {
    extern char *fs_easy_output_to_json(struct fieldstat_easy *fs, const struct timeval *timestamp, const struct timeval *timestamp_delta);
}

[[gnu::unused]] static struct fieldstat_easy *get_hist_fieldstat_easy()
{
    struct field global_tags[2];
    struct field cell_tags[2];

    global_tags[0].key  = "rule_id";
    global_tags[0].type = FIELD_VALUE_INTEGER;
    global_tags[0].value_longlong = 1;
    global_tags[1].key  = "action";
    global_tags[1].type = FIELD_VALUE_CSTRING;
    global_tags[1].value_str = "deny";

    cell_tags[0].key  = "thread_id";
    cell_tags[0].type = FIELD_VALUE_INTEGER;
    cell_tags[0].value_longlong = 1;
    cell_tags[1].key  = "hit_rate";
    cell_tags[1].type = FIELD_VALUE_DOUBLE;
    cell_tags[1].value_double = 1.1;

    const char *hist_names[] = {"list_num", "max_wt_ms", "ivt_nx_itv_ms", 
                                "bye_pv_itv_ms", "sess_num/udp", "ivt/udp",
                                "bye/udp", "oth_mtd/udp"};

    struct fieldstat_easy *fse = fieldstat_easy_new(N_THREAD, NULL, global_tags, 2);
    for(unsigned int i = 0; i < sizeof(hist_names)/sizeof(hist_names[0]); i++)
    {
        int hist_id = fieldstat_easy_register_histogram(fse, hist_names[i], 1, 600000, 3); // only precision 1 would be enough
        for(int j = 0; j < 100; j++)
        {
            fieldstat_easy_histogram_record(fse, rand() % N_THREAD, hist_id, cell_tags, 2, i*100 + j);
        }
    }

    return fse;
}

int test_fieldstat_cube_create(struct fieldstat *instance, const struct field *dimensions, size_t n_dimensions, enum sampling_mode mode, int k)
{
    assert(mode == SAMPLING_MODE_COMPREHENSIVE);
    int ret = fieldstat_cube_create(instance, dimensions, n_dimensions);
    fieldstat_cube_set_sampling(instance, ret, mode, k, 0);
    return ret;
}

static void write_hll(struct fieldstat *instance) {
    struct field shared_tags[1];

    shared_tags[0].key  = "rule_id";
    shared_tags[0].type = FIELD_VALUE_INTEGER;
    shared_tags[0].value_longlong = 1;

    const char *hll_name[] = {"external_ip", "internal_ip", "acc_ip"};
    int cube_id = test_fieldstat_cube_create(instance, shared_tags, 1, 
                                          SAMPLING_MODE_COMPREHENSIVE, 100);

    for(unsigned int i = 0; i < sizeof(hll_name) / sizeof(hll_name[0]); i++)
    {
        int hll_id  = fieldstat_register_hll(instance, cube_id, hll_name[i], 5);

        for(int j = 0; j < 100; j++)
        {
            char ip_str[64] = {0};
            memset(ip_str, 0, sizeof(ip_str));
            snprintf(ip_str, sizeof(ip_str), "192.168.%u.%d", i, j);
            int ret = fieldstat_hll_add(instance, cube_id, hll_id, NULL, 0, ip_str, strlen(ip_str));
            EXPECT_EQ(0, ret);
        }
    }
}

void write_histogram(struct fieldstat *instance) {
    struct field shared_tags[2];
    struct field cell_tags[2];

    shared_tags[0].key  = "rule_id";
    shared_tags[0].type = FIELD_VALUE_INTEGER;
    shared_tags[0].value_longlong = 1;
    shared_tags[1].key  = "action";
    shared_tags[1].type = FIELD_VALUE_CSTRING;
    shared_tags[1].value_str = "deny";

    cell_tags[0].key  = "thread_id";
    cell_tags[0].type = FIELD_VALUE_INTEGER;
    cell_tags[0].value_longlong = 1;
    cell_tags[1].key  = "hit_rate";
    cell_tags[1].type = FIELD_VALUE_DOUBLE;
    cell_tags[1].value_double = 1.1;

    const char *hist_names[] = {"list_num", "max_wt_ms", "ivt_nx_itv_ms", 
                                "bye_pv_itv_ms", "sess_num/udp", "ivt/udp",
                                "bye/udp", "oth_mtd/udp"};


    int cube_id = test_fieldstat_cube_create(instance, shared_tags, 2, 
                                          SAMPLING_MODE_COMPREHENSIVE, 100);

    for(unsigned int i = 0; i < sizeof(hist_names)/sizeof(hist_names[0]); i++)
    {
        int hist_id = fieldstat_register_histogram(instance, cube_id, hist_names[i], 1, 600000, 3);

        for(int j = 0; j < 100; j++)
        {
            fieldstat_histogram_record(instance, cube_id, hist_id, cell_tags, 2, i*100 + j);
        }
    }
}

// static struct fieldstat_easy *get_table_fieldstat_easy()
// {
//     struct field global_tags[2];
//     struct field cell_tags;

//     global_tags[0].key  = "policy_id";
//     global_tags[0].type = FIELD_VALUE_INTEGER;
//     global_tags[0].value_longlong = 1;
//     global_tags[1].key  = "quanlity";
//     global_tags[1].type = FIELD_VALUE_DOUBLE;
//     global_tags[1].value_double = 0.5;

//     const char *cell_tag_value[] = {
//         "sum", "SECURITY-EVENT", "SESSION-RECORD", "INTERNAL-RTP-RECORD",
//         "VOIP-RECORD", "INTERIM-SESSION-RECORD", "TRANSACTION-RECORD",
//         "GTPC-RECORD", "BGP-RECORD", "PROXY-EVENT", "DOS-SKETCH-RECORD",
//         "STATISTICS-RULE-METRIC", "OBJECT-STATISTICS-METRIC"};

//     cell_tags.key  = "send_log";
//     cell_tags.type = FIELD_VALUE_CSTRING;
//     cell_tags.value_str = "true";

//     struct fieldstat_easy *fse = fieldstat_easy_new(N_THREAD, NULL, global_tags, 2);
//     int counter_id_0 = fieldstat_easy_register_counter(fse, "T_success_log");
//     int counter_id_1 = fieldstat_easy_register_counter(fse, "T_fail_log");

//     for(unsigned int i = 0; i < sizeof(cell_tag_value)/sizeof(cell_tag_value[0]); i++)
//     {
//         cell_tags.value_str = cell_tag_value[i];
//         fieldstat_easy_counter_incrby(fse, rand()%N_THREAD, counter_id_0, &cell_tags, 1, 1);
//         if(i < 5)
//             fieldstat_easy_counter_incrby(fse, rand()%N_THREAD, counter_id_1, &cell_tags, 1, 2);
//     }

//     return fse;
// }

void fieldstat_easy_to_file(struct fieldstat_easy *fse, const char *file_path)
{
    fieldstat_easy_enable_auto_output(fse, file_path, 1);
    usleep(1 * 1000 * 1000 + 500 * 1000); // 1.5s
    fieldstat_easy_free(fse); // only by free to stop exporting
}

void write_table(struct fieldstat *instance) {
    struct field shared_tags[2];   
    shared_tags[0].key  = "policy_id";
    shared_tags[0].type = FIELD_VALUE_INTEGER;
    shared_tags[0].value_longlong = 1;
    shared_tags[1].key  = "quanlity";
    shared_tags[1].type = FIELD_VALUE_DOUBLE;
    shared_tags[1].value_double = 0.5;

    const char *cell_tag_value[] = {
        "sum", "SECURITY-EVENT", "SESSION-RECORD", "INTERNAL-RTP-RECORD",
        "VOIP-RECORD", "INTERIM-SESSION-RECORD", "TRANSACTION-RECORD",
        "GTPC-RECORD", "BGP-RECORD", "PROXY-EVENT", "DOS-SKETCH-RECORD",
        "STATISTICS-RULE-METRIC", "OBJECT-STATISTICS-METRIC"};

    struct field cell_tags;
    cell_tags.key = "send_log";
    cell_tags.type = FIELD_VALUE_CSTRING;
    cell_tags.value_str = "true";

    int cube_id = test_fieldstat_cube_create(instance, shared_tags, 2, 
                                          SAMPLING_MODE_COMPREHENSIVE, 100);

    int counter_id_0 = fieldstat_register_counter(instance, cube_id, "T_success_log");

    int counter_id_1 = fieldstat_register_counter(instance, cube_id, "T_fail_log");

    for(unsigned int i = 0; i < sizeof(cell_tag_value)/sizeof(cell_tag_value[0]); i++)
    {
        cell_tags.value_str = cell_tag_value[i];
        fieldstat_counter_incrby(instance, cube_id, counter_id_0, &cell_tags, 1, 1);
        if(i < 5)
            fieldstat_counter_incrby(instance, cube_id, counter_id_1, &cell_tags, 1, 2);
    }
}

static int write_json_to_file(const char *filename, char *json_str) 
{
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) 
    {
        return -1;
    }
    fprintf(fp, json_str);
    fclose(fp);

    return 0;
}

TEST(ExporterLocal, TableBuild)
{
    struct timeval current   = {100, 10000};
    struct fieldstat *merger = fieldstat_new();

    write_hll(merger);
    write_histogram(merger);
    write_table(merger);

    struct fieldstat_json_exporter *exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_enable_delta(exporter);
    char *str_json = fieldstat_json_exporter_export(exporter, merger, &current);
    
    int ret = write_json_to_file("/tmp/fieldstat.json",str_json);
    EXPECT_EQ(0, ret);
}

// #include <iostream>
// #include <fstream>
// TEST(ExporterLocal, TableBuild2)
// {
//     // fieldstat easy does not support hll
//     struct fieldstat_easy *hist = get_hist_fieldstat_easy();
//     struct fieldstat_easy *table = get_table_fieldstat_easy();

//     // fieldstat easy does not support merge
    
//     fieldstat_easy_to_file(hist, "/tmp/fieldstat_hist.json");
//     fieldstat_easy_to_file(table, "/tmp/fieldstat_table.json");

//     // merge to one file
//     std::ifstream file("/tmp/fieldstat_hist.json");
//     std::string json_hist((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
//     file.close();
//     file.open("/tmp/fieldstat_table.json");
//     std::string json_table((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
//     file.close();
    
//     cJSON *root_hist = cJSON_Parse(json_hist.c_str());
//     cJSON *root_table = cJSON_Parse(json_table.c_str());
//     cJSON *dst = cJSON_CreateArray();
//     cJSON *hist_o = cJSON_GetArrayItem(root_hist, 0);
//     cJSON *table_o = cJSON_GetArrayItem(root_table, 0);
//     cJSON_AddItemToArray(dst, hist_o);
//     cJSON_AddItemToArray(dst, table_o);
//     char *str_json = cJSON_Print(dst);
//     int ret = write_json_to_file("/tmp/fieldstat.json",str_json);
// }


int main(int argc, char *argv[]) 
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
