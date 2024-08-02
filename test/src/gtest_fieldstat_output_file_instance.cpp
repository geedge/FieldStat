#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "fieldstat_internal.h"

extern struct prometheus_endpoint_instance g_prometheus_endpoint_instance;

TEST(FeildStatOutput, OutputMaatTable)
{
	int ret = 0;
	struct fieldstat_instance * instance = NULL;
	int table_id = -1;
	int output_metric_ids[54] = {0};
	FILE *fp;
	int n_line = 0;
	int n_loops = 48;
	char line[2048] = {0};

	const char *output_file_path = "/tmp/fieldstat.txt"; 
	const char *table_column_name[] = {
		"rule", "reg", "v6", "stream", "scan_cnt", "scan_bytes", "scan_cpu_time", "hit_cnt"
	};
	const char *compared_table_row = "PXY_PROFILE_RUN_SCRIPTS           9223372036854775807"
									 "                   0                   0                   0"
									 "                   0                   0                   0"
									 "                   0\n";

	enum field_metric_type table_column_type[8] = {
		FIELD_METRIC_TYPE_GAUGE, FIELD_METRIC_TYPE_GAUGE, FIELD_METRIC_TYPE_GAUGE, FIELD_METRIC_TYPE_GAUGE, 
		FIELD_METRIC_TYPE_GAUGE, FIELD_METRIC_TYPE_GAUGE, FIELD_METRIC_TYPE_GAUGE, FIELD_METRIC_TYPE_GAUGE
	};

	instance = fieldstat_instance_new("maat");
	EXPECT_STREQ("maat", instance->name);

	table_id = fieldstat_register_table(instance, "maat_info", table_column_name, table_column_type, 
										sizeof(table_column_name)/sizeof(table_column_name[0]));
	EXPECT_EQ(0, table_id);
	ret = fieldstat_register_table_row(instance, table_id, "Sum", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_CTRL_COMPILE", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "GROUP_PXY_CTRL_COMPILE_RELATION", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "GROUP_GROUP_RELATION", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_IP", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_URL", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_HTTP_URL", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_FQDN", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_FQDN_CAT", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_HTTP_HOST", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_HTTP_HOST_CAT", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_HTTP_SIGNATURE", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_HTTP_REQ_HDR", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_HTTP_RES_HDR", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_KEYWORDS", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_HTTP_REQ_BODY", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_HTTP_RES_BODY", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_SUBSCRIBER_ID", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_APP_ID", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_SECURITY_SOURCE_ADDR", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_SECURITY_DESTINATION_ADDR", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_CACHE_COMPILE", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_CACHE_GROUP", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_CACHE_HTTP_URL", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_CACHE_HTTP_COOKIE", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_PROFILE_TRUSTED_CA_CERT", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_OBJ_TRUSTED_CA_CRL", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_PROFILE_RESPONSE_PAGES", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_PROFILE_HIJACK_FILES", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_PROFILE_INSERT_SCRIPTS", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_SECURITY_COMPILE", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_PROFILE_TRAFFIC_MIRROR", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_PROFILE_DECRYPTION", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_AS_NUMBER", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_SECURITY_SOURCE_ASN", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_SECURITY_DESTINATION_ASN", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_OBJ_GEO_LOCATION", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_SECURITY_SOURCE_LOCATION", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_SECURITY_DESTINATION_LOCATION", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_DOH_QNAME", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_DOH_HOST", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "TSG_FIELD_DOH_HOST_CAT", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_SSL_FINGERPRINT", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);
	ret = fieldstat_register_table_row(instance, table_id, "PXY_PROFILE_RUN_SCRIPTS", NULL, 0, output_metric_ids);
	EXPECT_EQ(0, ret);

	ret = fieldstat_set_local_output(instance, output_file_path, "default");
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, output_metric_ids[0], 9223372036854775807);
	sleep(2);

	fp = fopen("/tmp/fieldstat.txt", "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 46)
		{
			EXPECT_STREQ(compared_table_row, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);

	fieldstat_instance_free(instance);
}

TEST(FeildStatOutput, OutputMaatGaugeType)
{
	int ret = 0;
	struct fieldstat_instance * instance = NULL;
	int metric_id = -1;
	FILE *fp;
	int n_line = 0;
	int n_loops = 6;
	char line[2048] = {0};

	const char *output_file_path = "/tmp/fieldstat.txt"; 
	const char *compared_row = "     version: 922            threads: 0               tables: 0          plug_cached: 0             plug_acc: 0                group: 0         \n";

	instance = fieldstat_instance_new("maat");
	EXPECT_STREQ("maat", instance->name);

	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "version", NULL, 0);
	EXPECT_EQ(0, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "threads", NULL, 0);
	EXPECT_EQ(1, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "tables", NULL, 0);
	EXPECT_EQ(2, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "plug_cached", NULL, 0);
	EXPECT_EQ(3, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "plug_acc", NULL, 0);
	EXPECT_EQ(4, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "group", NULL, 0);
	EXPECT_EQ(5, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "not_grp", NULL, 0);
	EXPECT_EQ(6, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "compile", NULL, 0);
	EXPECT_EQ(7, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "garbage_num", NULL, 0);
	EXPECT_EQ(8, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "outer_mid", NULL, 0);
	EXPECT_EQ(9, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "z_stream", NULL, 0);
	EXPECT_EQ(10, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "nt_grp_hit", NULL, 0);
	EXPECT_EQ(11, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "scan_bytes", NULL, 0);
	EXPECT_EQ(12, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "scan_times", NULL, 0);
	EXPECT_EQ(13, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "update_err", NULL, 0);
	EXPECT_EQ(14, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "scan_error", NULL, 0);
	EXPECT_EQ(15, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "cmd_commit", NULL, 0);
	EXPECT_EQ(16, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "cmd_in_q", NULL, 0);
	EXPECT_EQ(17, metric_id);
	metric_id = fieldstat_register(instance, FIELD_METRIC_TYPE_GAUGE, "line_cmd/s", NULL, 0);
	EXPECT_EQ(18, metric_id);

	ret = fieldstat_set_local_output(instance, output_file_path, "default");
	EXPECT_EQ(0, ret);
	fieldstat_instance_start(instance);
	sleep(1);
	fieldstat_value_incrby(instance, 0, 922);
	sleep(2);

	fp = fopen("/tmp/fieldstat.txt", "r");
	EXPECT_NE(nullptr, fp);

	while(!feof(fp))
	{
		if(NULL == fgets(line, sizeof(line), fp))
		{
			continue;
		}
		if(n_line == 1)
		{
			EXPECT_STREQ(compared_row, line);
		}
		n_line++;
	}
	fclose(fp);
	EXPECT_EQ(n_line, n_loops);

	fieldstat_instance_free(instance);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}