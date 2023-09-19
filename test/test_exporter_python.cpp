#include <gtest/gtest.h>

#include "utils.hpp"
#include "cjson/cJSON.h"

#include "fieldstat.h"
#include "fieldstat_exporter.h"

static struct fieldstat *get_hll_fieldsstat()
{
    struct fieldstat_tag shared_tags[1];

    shared_tags[0].key  = "rule_id";
    shared_tags[0].type = TAG_INTEGER;
    shared_tags[0].value_longlong = 1;

    const char *hll_name[] = {"external_ip", "internal_ip", "acc_ip"};

    struct fieldstat *instance = fieldstat_new();
    EXPECT_NE(nullptr, instance);
    int cube_id = fieldstat_register_cube(instance, shared_tags, 1, 
                                          SAMPLING_MODE_COMPREHENSIVE, 100);

    for(unsigned int i = 0; i < sizeof(hll_name) / sizeof(hll_name[0]); i++)
    {
        int hll_id  = fieldstat_register_hll(instance, cube_id, hll_name[i], 5);
        int cell_id = fieldstat_cube_add(instance, cube_id, NULL, 0, 1000);

        for(int j = 0; j < 100; j++)
        {
            char ip_str[64] = {0};
            memset(ip_str, 0, sizeof(ip_str));
            snprintf(ip_str, sizeof(ip_str), "192.168.%u.%d", i, j);
            int ret = fieldstat_hll_add(instance, cube_id, hll_id, cell_id, ip_str, strlen(ip_str));
            EXPECT_EQ(0, ret);
        }

    }

    return instance;
}


static struct fieldstat *get_hist_fieldstat()
{
    struct fieldstat_tag shared_tags[2];
    struct fieldstat_tag cell_tags[2];

    shared_tags[0].key  = "rule_id";
    shared_tags[0].type = TAG_INTEGER;
    shared_tags[0].value_longlong = 1;
    shared_tags[1].key  = "action";
    shared_tags[1].type = TAG_CSTRING;
    shared_tags[1].value_str = "deny";

    cell_tags[0].key  = "thread_id";
    cell_tags[0].type = TAG_INTEGER;
    cell_tags[0].value_longlong = 1;
    cell_tags[1].key  = "hit_rate";
    cell_tags[1].type = TAG_DOUBLE;
    cell_tags[1].value_double = 1.1;

    const char *hist_names[] = {"list_num", "max_wt_ms", "ivt_nx_itv_ms", 
                                "bye_pv_itv_ms", "sess_num/udp", "ivt/udp",
                                "bye/udp", "oth_mtd/udp"};


    struct fieldstat *instance = fieldstat_new();
    EXPECT_NE(nullptr, instance);
    int cube_id = fieldstat_register_cube(instance, shared_tags, 2, 
                                          SAMPLING_MODE_COMPREHENSIVE, 100);
    EXPECT_EQ(0, cube_id);

    for(unsigned int i = 0; i < sizeof(hist_names)/sizeof(hist_names[0]); i++)
    {
        int hist_id = fieldstat_register_hist(instance, cube_id, hist_names[i], 1, 600000, 3);

        int cell_id = fieldstat_cube_add(instance, cube_id, cell_tags, 2, 1000);
        for(int j = 0; j < 100; j++)
        {
            fieldstat_hist_record(instance, cube_id, hist_id, cell_id, i*100 + j);
        }
    }

    return instance;
}



static struct fieldstat *get_table_fieldstat()
{
    struct fieldstat_tag shared_tags[2];   
    shared_tags[0].key  = "policy_id";
    shared_tags[0].type = TAG_INTEGER;
    shared_tags[0].value_longlong = 1;
    shared_tags[1].key  = "quanlity";
    shared_tags[1].type = TAG_DOUBLE;
    shared_tags[1].value_double = 0.5;

    const char *cell_tag_value[] = {
        "sum", "SECURITY-EVENT", "SESSION-RECORD", "INTERNAL-RTP-RECORD",
        "VOIP-RECORD", "INTERIM-SESSION-RECORD", "TRANSACTION-RECORD",
        "GTPC-RECORD", "BGP-RECORD", "PROXY-EVENT", "DOS-SKETCH-RECORD",
        "STATISTICS-RULE-METRIC", "OBJECT-STATISTICS-METRIC"};

    struct fieldstat_tag cell_tags;
    cell_tags.key = "send_log";
    cell_tags.type = TAG_CSTRING;
    cell_tags.value_str = "true";

    struct fieldstat *instance = fieldstat_new();
    EXPECT_NE(nullptr, instance);

    int cube_id = fieldstat_register_cube(instance, shared_tags, 2, 
                                          SAMPLING_MODE_COMPREHENSIVE, 100);
    EXPECT_EQ(0, cube_id);

    int counter_id_0 = fieldstat_register_counter(instance, cube_id, 
                                                  "T_success_log",
                                                  COUNTER_MERGE_BY_SUM);

    int counter_id_1 = fieldstat_register_counter(instance, cube_id, 
                                                  "T_fail_log",
                                                  COUNTER_MERGE_BY_SUM);

    for(unsigned int i = 0; i < sizeof(cell_tag_value)/sizeof(cell_tag_value[0]); i++)
    {
        cell_tags.value_str = cell_tag_value[i];
        int cell_id_0 = fieldstat_cube_add(instance, cube_id, &cell_tags, 1, 1);
        fieldstat_counter_incrby(instance, cube_id, counter_id_0, cell_id_0, 1);
        if(i < 5)
            fieldstat_counter_incrby(instance, cube_id, counter_id_1, cell_id_0, 2);
    }

    return instance;
}

TEST(ExporterLocal, TableBuild)
{
    struct timeval current   = {100, 10000};
    struct fieldstat *merger = fieldstat_new();
    struct fieldstat *hll    = get_hll_fieldsstat();
    struct fieldstat *hist   = get_hist_fieldstat();
    struct fieldstat *table  = get_table_fieldstat();

    fieldstat_merge(merger, hll);
    fieldstat_merge(merger, hist);
    fieldstat_merge(merger, table);
    struct fieldstat_json_exporter *exporter = fieldstat_json_exporter_new(merger);
    fieldstat_json_exporter_enable_delta(exporter);
    char *str_json = fieldstat_json_exporter_export(exporter, &current);
    printf(str_json);
}


int main(int argc, char *argv[]) 
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
