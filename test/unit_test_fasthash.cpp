
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
    struct my_hash_tag_key *key;
    int value;
    UT_hash_handle hh;
};

TEST(unit_test_fasthash, simple_add_and_find_when_length_is_1_given_different_type_of_value)
{
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;

    struct my_hash_tag_key *keys[3];
    keys[0] = my_hash_key_convert_from_fieldstat_tag(&TEST_TAG_INT, 1);
    keys[1] = my_hash_key_convert_from_fieldstat_tag(&TEST_TAG_DOUBLE, 1);
    keys[2] = my_hash_key_convert_from_fieldstat_tag(&TEST_TAG_STRING, 1);

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

    my_hash_tag_key_free(keys[0]);
    my_hash_tag_key_free(keys[1]);
    my_hash_tag_key_free(keys[2]);
}

TEST(unit_test_fasthash, simple_add_and_find_when_length_is_3_of_different_types)
{
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;
    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));

    const struct fieldstat_tag tags[3] = {TEST_TAG_INT, TEST_TAG_STRING, TEST_TAG_DOUBLE};

    s->key = my_hash_key_convert_from_fieldstat_tag(tags, 3);
    s->value = 321;
    HASH_ADD_TAG(hdr, key, s);
    printf("add finish \n");

    struct testing_hash *find = NULL;
    HASH_FIND_TAG(hdr, s->key, find);
    ASSERT_NE(find, nullptr);
    ASSERT_EQ(find->value, 321);

    HASH_DEL(hdr, find);

    my_hash_tag_key_free(s->key);
    free(s);
}

TEST(unit_test_fasthash, test_multiple_hash_and_ensure_no_double_hashing)
{
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;
    struct testing_hash *find[3];

    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));

    const struct fieldstat_tag tags[3] = {TEST_TAG_INT, TEST_TAG_STRING, TEST_TAG_DOUBLE};

    s->key = my_hash_key_convert_from_fieldstat_tag(tags, 3);
    s->value = 321;
    printf("call hash add tag\n");
    HASH_ADD_TAG(hdr, key, s);
    printf("add finish \n");

    struct my_hash_tag_key *new_key = my_hash_key_convert_from_fieldstat_tag(tags, 3);
    clock_t start = clock();
    printf("HASH_FIND_TAG 1\n");
    HASH_FIND_TAG(hdr, new_key, find[0]);
    clock_t end = clock();
    clock_t duration = end - start;
    printf("duration with new hash key: %ld \n", (long int)duration);

    start = clock();
    printf("HASH_FIND_TAG 2\n");
    HASH_FIND_TAG(hdr, s->key, find[1]);
    end = clock();
    clock_t duration2 = end - start;
    printf("duration with old hash key: %ld \n", (long int)duration2);

    start = clock();
    printf("HASH_FIND_TAG 3\n");
    HASH_FIND_TAG(hdr, new_key, find[2]);
    end = clock();
    clock_t duration3 = end - start;
    printf("duration with new hash key again: %ld \n", (long int)duration3);

    EXPECT_GT(duration, duration2);
    EXPECT_GT(duration, duration3);

    EXPECT_EQ(find[0], find[1]);
    EXPECT_EQ(find[0], find[2]);

    HASH_DEL(hdr, find[0]);
    my_hash_tag_key_free(s->key);
    free(s);
    my_hash_tag_key_free(new_key);
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}