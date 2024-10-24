#include <gtest/gtest.h>

#include "utils.hpp"
#include "cjson/cJSON.h"

#include "fieldstat.h"
#include "fieldstat_exporter.h"

void assert_cell_null(const struct fieldstat *instance, int cube_id, long long expected_count)
{
    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list[0].n_field, 0);
    EXPECT_TRUE(tag_list[0].field == NULL);
    long long value;
    fieldstat_counter_get(instance, cube_id, &tag_list[0], 0, &value);
    EXPECT_EQ(value, expected_count);
    fieldstat_field_list_arr_free(tag_list, n_cell);
}

TEST(test_empty_tag, add_many_times)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, NULL, 0);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_COMPREHENSIVE, 1, 0);

    fieldstat_counter_incrby(instance, cube_id, metric_id, NULL, 0, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, NULL, 0, 1);

    assert_cell_null(instance, cube_id, 2);

    fieldstat_free(instance);
}

struct fieldstat *test_empty_my_init(enum sampling_mode mode = SAMPLING_MODE_COMPREHENSIVE)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, NULL, 0);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric");
    fieldstat_cube_set_sampling(instance, cube_id, mode, 1, 0);

    fieldstat_counter_incrby(instance, cube_id, metric_id, NULL, 0, 1);

    return instance;
}

void assert_cell_uuid(const struct fieldstat *instance, int cube_id, long long expected_count)
{
    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list[0].n_field, 0);
    EXPECT_TRUE(tag_list[0].field == NULL);
    long long value;
    fieldstat_counter_get(instance, cube_id, &tag_list[0], 0, &value);
    EXPECT_EQ(value, expected_count);
    fieldstat_field_list_arr_free(tag_list, n_cell);
}

TEST(test_empty_tag, merge)
{
    struct fieldstat *instance_src = test_empty_my_init();
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance_dst, &ret_cube_id_arr, &n_cube);
    int ret_cube_id = ret_cube_id_arr[0];
    struct field_list *shared_tag = fieldstat_cube_get_dimension(instance_dst, ret_cube_id);
    EXPECT_EQ(shared_tag->n_field, 0);
    EXPECT_TRUE(shared_tag->field == NULL);
    fieldstat_field_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    assert_cell_null(instance_dst, ret_cube_id, 2);

    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_empty_tag, merge_topk)
{
    struct fieldstat *instance_src = test_empty_my_init(SAMPLING_MODE_TOPK);
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance_dst, &ret_cube_id_arr, &n_cube);
    int ret_cube_id = ret_cube_id_arr[0];
    struct field_list *shared_tag = fieldstat_cube_get_dimension(instance_dst, ret_cube_id);
    EXPECT_EQ(shared_tag->n_field, 0);
    EXPECT_TRUE(shared_tag->field == NULL);
    fieldstat_field_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    assert_cell_null(instance_dst, ret_cube_id, 2);


    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_empty_tag, merge_spreadsketch)
{
    struct fieldstat *instance_src = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance_src, NULL, 0);
    int metric_id = fieldstat_register_hll(instance_src, cube_id, "metric", 4);
    fieldstat_cube_set_sampling(instance_src, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 1, 0);
    fieldstat_hll_add(instance_src, cube_id, metric_id, NULL, 0, "1", 1);
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance_dst, &ret_cube_id_arr, &n_cube);
    int ret_cube_id = ret_cube_id_arr[0];
    struct field_list *shared_tag = fieldstat_cube_get_dimension(instance_dst, ret_cube_id);
    EXPECT_EQ(shared_tag->n_field, 0);
    EXPECT_TRUE(shared_tag->field == NULL);
    fieldstat_field_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dst, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list[0].n_field, 0);
    EXPECT_TRUE(tag_list[0].field == NULL);
    double value;
    fieldstat_hll_get(instance_dst, cube_id, &tag_list[0], 0, &value);
    EXPECT_NEAR(value, 1, 0.4);
    fieldstat_field_list_arr_free(tag_list, n_cell);

    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_empty_tag, export)
{
    struct fieldstat *instance = test_empty_my_init();
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    // std::cout << "json_string: \n" << json_string << std::endl;

    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);
    // check field
    cJSON *field = cJSON_GetObjectItem(root, "tags");
    EXPECT_EQ(cJSON_GetArraySize(field), 0);

    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    EXPECT_NE(metrics, nullptr);
    cJSON *counter = cJSON_GetObjectItem(metrics, "metric");
    EXPECT_EQ(counter->valueint, 1);

    cJSON_Delete(root_arr);
}

/* -------------------------------------------------------------------------- */
/*                                    UUID                                    */
/* -------------------------------------------------------------------------- */
class UuidTagContext {
    private:
        std::unordered_map<int, struct field *> fields;

    public:
        UuidTagContext() {
            
        }

        ~UuidTagContext() {
            for (auto it = fields.begin(); it != fields.end(); it++) {
                free((char *)it->second->key);
                free(it->second);
            }
        }

        struct field *get(int index) {
            if (fields.find(index) != fields.end()) {
                return fields[index];
            } else {
                uuid_t uuid;
                uuid_generate(uuid);
                struct field *field = (struct field *)malloc(sizeof(struct field));
                field->type = FIELD_VALUE_UUID;
                memcpy(field->value_uuid, uuid, sizeof(uuid_t));
                field->key = (const char *)malloc(10);
                sprintf((char *)field->key, "key_%d", index);

                fields[index] = field;
                return field;
            }
        }
        bool check(int index, struct field *field) {
            if (fields.find(index) == fields.end()) {
                return false;
            }
            return uuid_compare(fields[index]->value_uuid, field->value_uuid) == 0;
        }
        bool check(int index, const char *uuid_str) {
            if (fields.find(index) == fields.end()) {
                return false;
            }

            uuid_t uuid;
            uuid_parse(uuid_str, uuid);
            return uuid_compare(fields[index]->value_uuid, uuid) == 0;
        }
};

void check_cell_uuid(const struct fieldstat *instance, int cube_id, int index, long long expected_count, UuidTagContext &context)
{
    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list[0].n_field, 1);
    EXPECT_TRUE(context.check(index, &tag_list[0].field[0]));
    long long value;
    fieldstat_counter_get(instance, cube_id, &tag_list[0], 0, &value);
    EXPECT_EQ(value, expected_count);
    fieldstat_field_list_arr_free(tag_list, n_cell);
}

TEST(test_uuid_tag, add_many_times)
{
    UuidTagContext context;
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, context.get(0), 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric");
    fieldstat_cube_set_sampling(instance, cube_id, SAMPLING_MODE_COMPREHENSIVE, 1, 0);

    fieldstat_counter_incrby(instance, cube_id, metric_id, context.get(1), 1, 1);
    fieldstat_counter_incrby(instance, cube_id, metric_id, context.get(1), 1, 1);

    check_cell_uuid(instance, cube_id, 1, 2, context);

    fieldstat_free(instance);
}

struct fieldstat *test_uuid_tag_my_init(UuidTagContext &context,enum sampling_mode mode = SAMPLING_MODE_COMPREHENSIVE)
{
    struct fieldstat *instance = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance, context.get(0), 1);
    int metric_id = fieldstat_register_counter(instance, cube_id, "metric");
    fieldstat_cube_set_sampling(instance, cube_id, mode, 1, 0);

    fieldstat_counter_incrby(instance, cube_id, metric_id, context.get(1), 1, 1);

    return instance;
}

TEST(test_uuid_tag, merge)
{
    UuidTagContext context;
    struct fieldstat *instance_src = test_uuid_tag_my_init(context);
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance_dst, &ret_cube_id_arr, &n_cube);
    int ret_cube_id = ret_cube_id_arr[0];
    struct field_list *shared_tag = fieldstat_cube_get_dimension(instance_dst, ret_cube_id);
    EXPECT_TRUE(context.check(0, &shared_tag->field[0]));
    fieldstat_field_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    check_cell_uuid(instance_dst, ret_cube_id, 1, 2, context);

    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_uuid_tag, merge_topk)
{
    UuidTagContext context;
    struct fieldstat *instance_src = test_uuid_tag_my_init(context, SAMPLING_MODE_TOPK);
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    int *ret_cube_id_arr = NULL;
    int n_cube = 0;
    fieldstat_get_cubes(instance_dst, &ret_cube_id_arr, &n_cube);
    int ret_cell_id = ret_cube_id_arr[0];
    struct field_list *shared_tag = fieldstat_cube_get_dimension(instance_dst, ret_cell_id);
    EXPECT_EQ(shared_tag->n_field, 1);
    EXPECT_TRUE(context.check(0, &shared_tag->field[0]));
    fieldstat_field_list_arr_free(shared_tag, 1);
    free(ret_cube_id_arr);

    check_cell_uuid(instance_dst, ret_cell_id, 1, 2, context);

    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_uuid_tag, merge_spreadsketch)
{
    UuidTagContext context;
    struct fieldstat *instance_src = fieldstat_new();
    int cube_id = fieldstat_cube_create(instance_src, context.get(0), 1);
    int metric_id = fieldstat_register_hll(instance_src, cube_id, "metric", 4);
    fieldstat_cube_set_sampling(instance_src, cube_id, SAMPLING_MODE_TOP_CARDINALITY, 1, 0);
    fieldstat_hll_add(instance_src, cube_id, metric_id, context.get(1), 1, "1", 1);
    struct fieldstat *instance_dst = fieldstat_new();

    fieldstat_merge(instance_dst, instance_src);
    fieldstat_merge(instance_dst, instance_src);

    struct field_list *tag_list = NULL;
    size_t n_cell = 0;
    fieldstat_cube_get_cells(instance_dst, cube_id, &tag_list, &n_cell);
    EXPECT_EQ(n_cell, 1);
    EXPECT_EQ(tag_list[0].n_field, 1);
    EXPECT_TRUE(context.check(1, &tag_list[0].field[0]));
    double value;
    fieldstat_hll_get(instance_dst, cube_id, &tag_list[0], 0, &value);
    EXPECT_NEAR(value, 1, 0.4);
    fieldstat_field_list_arr_free(tag_list, n_cell);

    fieldstat_free(instance_dst);
    fieldstat_free(instance_src);
}

TEST(test_uuid_tag, export)
{
    UuidTagContext context;
    struct fieldstat *instance = test_uuid_tag_my_init(context);
    struct fieldstat_json_exporter *fieldstat_json_exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_set_global_dimension(fieldstat_json_exporter, context.get(2), 1);

    char *json_string = fieldstat_json_exporter_export(fieldstat_json_exporter, instance, &TEST_TIMEVAL);
    // std::cout << "json_string: \n" << json_string << std::endl;

    cJSON *root_arr = cJSON_Parse(json_string);
    free(json_string);
    fieldstat_json_exporter_free(fieldstat_json_exporter);
    fieldstat_free(instance);

    cJSON *root = cJSON_GetArrayItem(root_arr, 0);

    // check field(3 tags)
    bool ok_index[3] = {false, false, false};
    cJSON *field = cJSON_GetObjectItem(root, "tags");
    EXPECT_EQ(cJSON_GetArraySize(field), 3);

    for (int index_json = 0; index_json < 3; index_json++) {
        cJSON *uuid = cJSON_GetArrayItem(field, index_json);
        char *uuid_str = cJSON_GetObjectItem(field, uuid->string)->valuestring;

        for (int index = 0; index < 3; index++) {
            if (ok_index[index]) {
                continue;
            }

            if (context.check(index, uuid_str)) {
                ok_index[index] = true;
                break;
            }
        }
    }
    for (int index = 0; index < 3; index++) {
        EXPECT_TRUE(ok_index[index]);
    }

    cJSON *metrics = cJSON_GetObjectItem(root, "fields");
    EXPECT_NE(metrics, nullptr);
    cJSON *counter = cJSON_GetObjectItem(metrics, "metric");
    EXPECT_EQ(counter->valueint, 1);

    cJSON_Delete(root_arr);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}