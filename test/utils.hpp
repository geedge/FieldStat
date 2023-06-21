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
    explicit Fieldstat_tag_list_wrapper(struct fieldstat_tag_list *tag_list_c);
    explicit Fieldstat_tag_list_wrapper(std::uniform_int_distribution<int>& dist); // used by static_cast, in Api_caller::generateArgument
    ~Fieldstat_tag_list_wrapper();

    const struct fieldstat_tag *get_tag();
    size_t get_tag_count();

private:
    struct fieldstat_tag_list tag_list_c;
};

template <typename ReturnType, typename... Args>
class Api_caller {
public:
    // Constructor
    // Api_caller() : rng(std::random_device()()), distribution(1, 100) {}
    explicit Api_caller();

    // Member function to add API functions
    void addApiFunction(const std::function<ReturnType(Args...)>& apiFunction);

    // Call all the registered API functions with generated arguments and record inputs/outputs
    void testApis();
    void testApis(int loop_num);
    std::vector<std::tuple<Args...>>getArguments();

private:
    std::vector<std::function<ReturnType(Args...)>> apiFunctions;
    std::mt19937 rng;
    std::uniform_int_distribution<int> distribution;
    std::vector<std::tuple<Args...>> arguments;
    std::vector<ReturnType> results;

    // Generate random arguments for a function
    std::vector<std::tuple<Args...>> generateArguments();
    template<size_t Index>
    void generateArgumentsImpl(std::vector<std::tuple<Args...>>& args, std::tuple<Args...>& current);
    template<size_t Index>
    void generateArgumentsImpl(std::vector<std::tuple<Args...>>& args);
    template<typename Arg>
    Arg generateArgument();

    ReturnType callApiFunction(const std::function<ReturnType(Args...)>& apiFunction, const std::tuple<Args...>& args);
    void recordInputOutput(Args... args, ReturnType result);
};

template <typename Query_ret>
class Query_caller {
public:
    explicit Query_caller(const std::function<Query_ret(Fieldstat_tag_list_wrapper &)>& queryFunction);

    template <typename... Args>
    void take_args(std::vector<std::tuple<Args...>>& args);

    std::vector<Query_ret> get_results();

private:
    std::function<Query_ret(Fieldstat_tag_list_wrapper &)> queryFunction;
    std::vector<Fieldstat_tag_list_wrapper> arguments;

    template <typename... Args>
    void extract_tags(std::tuple<Args...>& args);
};

// query_ret must have operator== defined. if a element in results is found in expected_results, it will be counted as a hit.
template <typename Query_ret>
double cal_accuracy(std::vector<Query_ret>& results, std::vector<Query_ret>& expected_results);

