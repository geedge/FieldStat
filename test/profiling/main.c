#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "fieldstat.h"

#define ADD_OPER_NUM 1000000
#define MAX_STRING_KEY_LEN 10

const struct fieldstat_tag TEST_TAG_INT = {"INT key_", TAG_INTEGER, {.value_int = 100}};


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

void profiling_counter()
{
    printf("profiling_counter\n");
    getchar();
    struct fieldstat *instance = fieldstat_new();
    int id_added = fieldstat_register_counter(instance, "add");
    for (int i = 0; i < ADD_OPER_NUM; i++)
    {
        fieldstat_counter_incrby(instance, id_added, 1, &TEST_TAG_INT, 1);
    }
    fieldstat_free(instance);
}

void call_dynamic_key_build()
{

}

void bin_print_struct(void *obj, size_t sizeof_obj)
{
    unsigned char *p = (unsigned char *)obj;
    printf("sizeof_obj: %lu\n", sizeof_obj);
    for (int i = 0; i < sizeof_obj; i++)
    {
        printf("%02x ", p[i]);
    }
    printf("\n");
}

void try_hash_compound_key()
{
    struct fieldstat_tag tag1 = {"key1", TAG_INTEGER, {.value_int = 100}};
    struct fieldstat_tag tag2 = {"key2", TAG_DOUBLE, {.value_double = 100.0}};
    struct fieldstat_tag tag3 = {"key3", TAG_CSTRING, {.value_str = {'1', '2', '3', '\0'}}};
    struct fieldstat_tag tag_arr[] = {
        {"key1", TAG_INTEGER, {.value_int = 100}},
        {"key2", TAG_DOUBLE, {.value_double = 100.0}},
        {"key3", TAG_CSTRING, {.value_str = {'1', '2', '3', '\0'}}},
    };

    bin_print_struct(&tag1, sizeof(tag1));
    bin_print_struct(&tag2, sizeof(tag2));
    bin_print_struct(&tag3, sizeof(tag3));
    bin_print_struct(tag_arr, sizeof(tag_arr));
}

int main () {
    // profiling_counter();
    try_hash_compound_key();
}