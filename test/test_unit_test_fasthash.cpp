
#include <gtest/gtest.h>
#include "fieldstat.h"
#include "utils.hpp"
#include "tags/my_ut_hash.h"

struct testing_hash {
    struct my_hash_tag *key;
    size_t key_len;
    int value;
    UT_hash_handle hh;
};

TEST(unit_test_fasthash, add_and_find_when_length_is_1_given_int_value)
{
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;
    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));
    s->key = convert_fieldstat_tag_arr_to_my_hash_tag(&TEST_TAG_INT, 1);
    s->key_len = 1;
    s->value = 123;
    HASH_ADD_TAG(hh, hdr, key, 1, s);

    printf("add finish \n");

    struct testing_hash *find = NULL;
    HASH_FIND(hh, hdr, s->key, 1, find);
    ASSERT_NE(find, nullptr);
    ASSERT_EQ(find->value, 123);

    HASH_DEL(hdr, find);

    free(s->key->key);
    free(s->key->value);
    free(s->key);
    free(s);
}

TEST(unit_test_fasthash, add_and_find_when_length_is_2_given_str_and_int_value)
{
    struct fieldstat_tag tags[] = {TEST_TAG_STRING, TEST_TAG_INT};
    
    struct testing_hash *hdr = NULL;
    struct testing_hash *s = NULL;
    s = (struct testing_hash *)malloc(sizeof(struct testing_hash));
    s->key = convert_fieldstat_tag_arr_to_my_hash_tag(tags, 2);
    s->value = 123;
    HASH_ADD_TAG(hh, hdr, key, 2, s);

    printf("add finish \n");

    struct testing_hash *find = NULL;
    HASH_FIND(hh, hdr, s->key, 2, find);
    ASSERT_NE(find, nullptr);
    ASSERT_EQ(find->value, 123);

    HASH_DEL(hdr, find);

    for (int i = 0; i < 2; i++) {
        free(s->key[i].key);
        free(s->key[i].value);
    }
    free(s->key);
    free(s);
}


int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}