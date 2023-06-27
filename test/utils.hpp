#include <string>
#include "fieldstat.h"

#define RAND_SEED 1

void set_tag_num(int num);
int get_tag_num();

const struct fieldstat_tag TEST_TAG_STRING = {"STRING KEY_", TAG_CSTRING, {.value_str = "100.1.1.2"}};
const struct fieldstat_tag TEST_TAG_STRING_collided = {"collided", TAG_CSTRING, {.value_str = "2"}};
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
