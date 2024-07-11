#include <string>
#include <vector>
#include <unordered_map>
#include "fieldstat.h"

const struct field TEST_TAG_STRING = {"STRING KEY_", TAG_CSTRING, {.value_str = "100.1"}};
const struct field_list TEST_TAG_LIST_STRING = {(struct field *)&TEST_TAG_STRING, 1};
const struct field TEST_TAG_STRING_collided = {"collided", TAG_CSTRING, {.value_str = "2"}};
const struct field TEST_TAG_INT = {"INT key_", TAG_INTEGER, {.value_longlong = 100}};
const struct field_list TEST_TAG_LIST_INT = {(struct field *)&TEST_TAG_INT, 1};
const struct field TEST_TAG_INT_collided = {"collided", TAG_INTEGER, {.value_longlong = 2}};
const struct field TEST_TAG_DOUBLE = {"DOUBLE key_", TAG_DOUBLE, {.value_double = 100.1}};
const struct field_list TEST_TAG_LIST_DOUBLE = {(struct field *)&TEST_TAG_DOUBLE, 1};
const struct field TEST_TAG_DOUBLE_collided = {"collided", TAG_DOUBLE, {.value_double = 2.0}};
const struct field TEST_SHARED_TAG = {"shared", TAG_INTEGER, {.value_longlong = 1}};
const struct timeval TEST_TIMEVAL = {100, 10000};
const long long TEST_TIMEVAL_LONG = 100010; // 100s * 1000 + 10000us / 1000 = 100010ms

std::string gen_rand_string(int len);

class Fieldstat_tag_list_wrapper {
public:
    explicit Fieldstat_tag_list_wrapper(const struct field_list *tag_list_c);
    explicit Fieldstat_tag_list_wrapper(const char * key, int value);
    explicit Fieldstat_tag_list_wrapper(const char * key, const char *value);
    explicit Fieldstat_tag_list_wrapper(std::uniform_int_distribution<int> &dist, int tag_count);
    std::string to_string() const;
    // std::uniform_int_distribution<int> dist(1,100)
    explicit Fieldstat_tag_list_wrapper();
    Fieldstat_tag_list_wrapper(const Fieldstat_tag_list_wrapper &tag_list_wrapper);
    ~Fieldstat_tag_list_wrapper();
    bool operator==(const Fieldstat_tag_list_wrapper &tag_list_wrapper) const;

    const struct field *get_tag() const;
    size_t get_tag_count() const;
    const struct field_list *get_c_struct() const;
    void print_tag_list() const;
    Fieldstat_tag_list_wrapper& sort_tag_list();
private:
    struct field_list tag_list_c;
};


double test_cal_topk_accuracy(std::vector<struct Fieldstat_tag_list_wrapper *> &test_result, std::unordered_map<std::string, int> &expected_count);

// after we change fieldstat_counter_get return a error code in, all the tests should change correspondingly, so just use a adapter aliasing the old function
long long my_fieldstat_counter_get(const struct fieldstat *instance, int cube_id, int metric_id, int cell_id);


struct Flow {
    std::string src_ip;
    std::string dst_ip;
};

class SpreadSketchZipfGenerator {
private:
    const int MAX_DATA = 1000000;
    std::vector<std::pair<std::string, std::string>> *loadeds;
    unsigned cursor;

public:
    SpreadSketchZipfGenerator(double alpha, int n);
    struct Flow next();
    ~SpreadSketchZipfGenerator();

    double _alpha;
    int _n;
};