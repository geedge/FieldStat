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
    struct fieldstat_tag tag3 = {"key3", TAG_CSTRING, {.value_str = "123"}};
    struct fieldstat_tag tag_arr[] = {
        {"key1", TAG_INTEGER, {.value_int = 100}},
        {"key2", TAG_DOUBLE, {.value_double = 100.0}},
        {"key3", TAG_CSTRING, {.value_str = "123"}},
    };

    bin_print_struct(&tag1, sizeof(tag1));
    bin_print_struct(&tag2, sizeof(tag2));
    bin_print_struct(&tag3, sizeof(tag3));
    bin_print_struct(tag_arr, sizeof(tag_arr));
}

void performance_cube_add_comprehensive()
{
    printf("counter_performance\n");
    struct fieldstat_tag tags[64000];
    for (int i = 0; i < 64000; i++) {
        tags[i] = TEST_TAG_INT;
        tags[i].value_int = i;
    }
    getchar();
    clock_t start, end;
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_COMPREHENSIVE, 64000);
    fieldstat_register_counter(instance, 0, "test", 0);
    start = clock();
    for (int i = 0; i < 64000; i++) {
        (void)fieldstat_cube_add(instance, 0, &tags[i], 1, 1);
    }
    end = clock();
    printf("time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);

    fieldstat_free(instance);
}

extern void build_dynamic_cell_key(const struct fieldstat_tag tags[], size_t n_tags, char **out_key, size_t *out_key_size);
void build_dynamic_cell_key_performance()
{
    struct fieldstat_tag tags[100000];
    for (int i = 0; i < 100000; i++) {
        tags[i] = TEST_TAG_INT;
        tags[i].value_int = i;
    }
    char *key;
    size_t key_size;
    clock_t start, end;
    start = clock();
    printf("start\n");
    for (int i = 0; i < 100000; i++) {
        build_dynamic_cell_key(&tags[i], 1, &key, &key_size);
        free(key);
    }
    end = clock();
    printf("time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
}

void topk_K_100_tag_2()
{
    printf("topk_K_100_tag_2\n");
    getchar();
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_TOPK, 1000);
    fieldstat_register_counter(instance, 0, "test", 0);
    struct fieldstat_tag server_ip_tag = {"server_ip", TAG_CSTRING, {.value_str = "192.168.0.1"}};
    char client_ip[] = "123.123.123.123.1234";
    struct fieldstat_tag client_ip_tag = {"client_ip", TAG_CSTRING, {.value_str = client_ip}};
    struct fieldstat_tag *total = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * 2);
    for (int i = 0; i < 10000; i++) {
        char new_ip[sizeof(client_ip)];
        snprintf(new_ip, sizeof(new_ip), "%d.%d.%d.%d", rand() % 256, rand() % 256, rand() % 256, rand() % 256);
        client_ip_tag.value_str = new_ip;
        total[0] = server_ip_tag;
        total[1] = client_ip_tag;
        long long int increment = rand() % 5000;
        int cell_id = fieldstat_cube_add(instance, 0, total, 2, increment);
        if (cell_id == -1) {
            continue;
        }
        fieldstat_counter_incrby(instance, 0, 0, cell_id, increment);
    }
    
    fieldstat_free(instance);
    getchar();
}

int my_cmp_int(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

int topk_get_max_cell_id(int K, int *tag_vals, int n_tag)
{
    struct fieldstat *instance = fieldstat_new();
    fieldstat_register_cube(instance, &TEST_TAG_INT, 1, SAMPLING_MODE_TOPK, K);
    fieldstat_register_counter(instance, 0, "test", 0);
    struct fieldstat_tag tmp_tag = TEST_TAG_INT;

    for (int i = 0; i < n_tag; i++) {
        tmp_tag.value_int = tag_vals[i];
        int cell_id = fieldstat_cube_add(instance, 0, &tmp_tag, 1, 1);
        if (cell_id == -1) {
            continue;
        }
        fieldstat_counter_incrby(instance, 0, 0, cell_id, 1);
    }
    

}

int main () {
    // try_hash_compound_key();
    // topk_K_100_tag_2();
    // performance_cube_add_comprehensive();
    build_dynamic_cell_key_performance();
}