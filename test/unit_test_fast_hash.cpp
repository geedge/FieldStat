
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "fieldstat.h"
#include "utils.hpp"
#include "tags/my_ut_hash_inner.h"
#include "tags/my_ut_hash.h"
// #include "tags/sorted_set.h"

struct testing_hash {
    struct tag_hash_key *key;
    int value;
    UT_hash_handle hh;
};

TEST(unit_test_fasthash, simple_add_and_find_when_length_is_1_given_different_type_of_value)
{
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;

    struct tag_hash_key *keys[3];
    keys[0] = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_INT, 1);
    keys[1] = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_DOUBLE, 1);
    keys[2] = tag_hash_key_construct_with_fieldstat_tag(&TEST_TAG_STRING, 1);

    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));
    s->key = keys[0];
    s->value = 1;
    HASH_ADD_TAG(hdr, key, s);
    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));
    s->key = keys[1];
    s->value = 2;
    HASH_ADD_TAG(hdr, key, s);
    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));
    s->key = keys[2];
    s->value = 3;
    HASH_ADD_TAG(hdr, key, s);
    printf("add finish \n");

    struct testing_hash *find = NULL;
    HASH_FIND_TAG(hdr, keys[0], find);
    ASSERT_NE(find, nullptr);
    EXPECT_EQ(find->value, 1);
    HASH_DEL(hdr, find);
    free(find);

    find = NULL;
    HASH_FIND_TAG(hdr, keys[2], find);
    ASSERT_NE(find, nullptr);
    EXPECT_EQ(find->value, 3);
    HASH_DEL(hdr, find);
    free(find);

    find = NULL;
    HASH_FIND_TAG(hdr, keys[1], find);
    ASSERT_NE(find, nullptr);
    EXPECT_EQ(find->value, 2);
    HASH_DEL(hdr, find);
    free(find);

    tag_hash_key_free(keys[0]);
    tag_hash_key_free(keys[1]);
    tag_hash_key_free(keys[2]);
}

TEST(unit_test_fasthash, simple_add_and_find_when_length_is_3_of_different_types)
{
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;
    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));

    const struct fieldstat_tag tags[3] = {TEST_TAG_INT, TEST_TAG_STRING, TEST_TAG_DOUBLE};

    s->key = tag_hash_key_construct_with_fieldstat_tag(tags, 3);
    s->value = 321;
    HASH_ADD_TAG(hdr, key, s);
    printf("add finish \n");

    struct testing_hash *find = NULL;
    HASH_FIND_TAG(hdr, s->key, find);
    ASSERT_NE(find, nullptr);
    ASSERT_EQ(find->value, 321);

    HASH_DEL(hdr, find);

    tag_hash_key_free(s->key);
    free(s);
}

TEST(unit_test_fasthash, test_multiple_hash_and_ensure_no_double_hashing)
{
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;
    struct testing_hash *find[3];

    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));

    const struct fieldstat_tag tags[3] = {TEST_TAG_INT, TEST_TAG_STRING, TEST_TAG_DOUBLE};

    s->key = tag_hash_key_construct_with_fieldstat_tag(tags, 3);
    s->value = 321;
    printf("call hash add tag\n");
    HASH_ADD_TAG(hdr, key, s);
    printf("add finish \n");

    struct tag_hash_key *new_key = tag_hash_key_construct_with_fieldstat_tag(tags, 3);
    clock_t start = clock();
    HASH_FIND_TAG(hdr, new_key, find[0]);
    clock_t end = clock();
    clock_t duration = end - start;
    printf("duration with new hash key: %ld \n", (long int)duration);

    start = clock();
    HASH_FIND_TAG(hdr, s->key, find[1]);
    end = clock();
    clock_t duration2 = end - start;
    printf("duration with old hash key: %ld \n", (long int)duration2);

    start = clock();
    HASH_FIND_TAG(hdr, new_key, find[2]);
    end = clock();
    clock_t duration3 = end - start;
    printf("duration with new hash key again: %ld \n", (long int)duration3);

    EXPECT_GE(duration, duration2);
    EXPECT_GE(duration, duration3);

    EXPECT_EQ(find[0], find[1]);
    EXPECT_EQ(find[0], find[2]);

    HASH_DEL(hdr, find[0]);
    tag_hash_key_free(s->key);
    free(s);
    tag_hash_key_free(new_key);
}

TEST(unit_test_fasthash, serialize_and_deserialize)
{
    const struct fieldstat_tag tags[3] = {TEST_TAG_INT, TEST_TAG_STRING, TEST_TAG_DOUBLE};
    struct tag_hash_key *new_key = tag_hash_key_construct_with_fieldstat_tag(tags, 3);
    char *blob = NULL;
    size_t blob_size = 0;
    tag_hash_key_serialize(new_key, &blob, &blob_size);
    struct tag_hash_key *deserialized_key = tag_hash_key_deserialize(blob, blob_size);

    struct fieldstat_tag *deserialized_tags = NULL;
    size_t n_deserialized_tags = 0;
    tag_hash_key_convert_to_fieldstat_tag(deserialized_key, &deserialized_tags, &n_deserialized_tags);

    EXPECT_EQ(n_deserialized_tags, 3);
    EXPECT_STREQ(deserialized_tags[0].key, TEST_TAG_INT.key);
    EXPECT_STREQ(deserialized_tags[1].key, TEST_TAG_STRING.key);
    EXPECT_STREQ(deserialized_tags[2].key, TEST_TAG_DOUBLE.key);
    EXPECT_EQ(deserialized_tags[0].value_longlong, TEST_TAG_INT.value_longlong);
    EXPECT_STREQ(deserialized_tags[1].value_str, TEST_TAG_STRING.value_str);
    EXPECT_EQ(deserialized_tags[2].value_double, TEST_TAG_DOUBLE.value_double);

    printf("0\n");

    tag_hash_key_free(new_key);
    printf("1\n");
    tag_hash_key_free(deserialized_key);
    printf("2\n");

    free(blob);
    printf("3\n");

    for (size_t i = 0; i < n_deserialized_tags; i++)
    {
        free((char *)deserialized_tags[i].key);
        if (deserialized_tags[i].type == TAG_CSTRING) {
            free((char *)deserialized_tags[i].value_str);
        }
    }
    printf("4\n");

    free(deserialized_tags);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}