#include <string>
#include "fieldstat.h"

#define RAND_SEED 1

void set_tag_num(int num);
int get_tag_num();

const struct fieldstat_tag TEST_TAG_STRING = {"STRING KEY_", TAG_CSTRING, {.value_str = {'1','0','0','.','1','.','1','\0'}}};
const struct fieldstat_tag TEST_TAG_STRING_collided = {"collided", TAG_CSTRING, {.value_str = {'2','\0'}}};
const struct fieldstat_tag TEST_TAG_INT = {"INT key_", TAG_INTEGER, {.value_int = 100}};
const struct fieldstat_tag TEST_TAG_INT_collided = {"collided", TAG_INTEGER, {.value_int = 2}};
const struct fieldstat_tag TEST_TAG_DOUBLE = {"DOUBLE key_", TAG_DOUBLE, {.value_double = 100.1}};
const struct fieldstat_tag TEST_TAG_DOUBLE_collided = {"collided", TAG_DOUBLE, {.value_double = 2.0}};

std::string gen_rand_string(int len);


// class Fieldstat_tag_wrapper {
// public:
//     explicit Fieldstat_tag_wrapper(const char *key, long long value);
//     explicit Fieldstat_tag_wrapper(const char *key, double value);
//     explicit Fieldstat_tag_wrapper(const char *key, const char *value);
//     Fieldstat_tag_wrapper::Fieldstat_tag_wrapper(const struct fieldstat_tag *tag);
//     explicit Fieldstat_tag_wrapper(const std::uniform_int_distribution<int>& dist); // used by static_cast, in Api_caller::generateArgument
//     ~Fieldstat_tag_wrapper();
//     bool operator==(const Fieldstat_tag_wrapper& rhs) const;

//     const struct fieldstat_tag& get_c_struct() const;

// private:
//     struct fieldstat_tag tag;
// };

class Fieldstat_tag_list_wrapper {
public:
    explicit Fieldstat_tag_list_wrapper(const struct fieldstat_tag_list *tag_list_c);
    explicit Fieldstat_tag_list_wrapper(std::uniform_int_distribution<int> &dist, int tag_count);
    // std::uniform_int_distribution<int> dist(1,100)
    explicit Fieldstat_tag_list_wrapper();
    Fieldstat_tag_list_wrapper(const Fieldstat_tag_list_wrapper &tag_list_wrapper);
    ~Fieldstat_tag_list_wrapper();

    const struct fieldstat_tag *get_tag() const;
    size_t get_tag_count() const;
    const struct fieldstat_tag_list *get_c_struct() const;
    void print_tag_list() const;
private:
    struct fieldstat_tag_list tag_list_c;
};

struct fieldstat *test_serialize_and_deserialize(struct fieldstat *instance);
void test_merge_with_blob(struct fieldstat *dest, const struct fieldstat *src);
void test_merge_with_blob_twice(struct fieldstat *dest, const struct fieldstat *src);

template <typename Query_ret>
std::vector<Query_ret> test_get_query_result(Query_ret (*query)(const struct fieldstat *, int field_id, const struct fieldstat_tag_list *),
    struct fieldstat *instance, int queried_field_id)
{
    size_t cell_num;
    struct fieldstat_tag_list *tag_list = NULL;
    fieldstat_get_tag_lists(instance, queried_field_id, &tag_list, &cell_num);

    printf("fieldstat_get_tag_lists, cell num: %zu\n", cell_num);
    std::vector<Query_ret> ret_vec;
    for (size_t i = 0; i < cell_num; i++) {
        Query_ret ret = query(instance, queried_field_id, &tag_list[i]);
        ret_vec.push_back(ret);
    }
    if (cell_num > 0){
        for (size_t i = 0; i < cell_num; i++) {
            free(tag_list[i].tag);
        }
        free(tag_list);
    }

    return ret_vec;
}