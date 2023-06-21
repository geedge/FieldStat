

#include <string>
#include <stdlib.h>
#include <iostream>
#include <type_traits>
#include <vector>
#include <functional>
#include <chrono>
#include <random>
#include <tuple>
#include <string.h>
#include <algorithm>

#include "fieldstat.h"
#include "utils.hpp"

using namespace std;

static int g_tag_num = 10;

void set_tag_num(int num)
{
    g_tag_num = num;
}

int get_tag_num()
{
    return g_tag_num;
}

string gen_rand_string(int len)
{
    string s;
    for (int i = 0; i < len; i++)
    {
        s += 'a' + rand() % 26;
    }
    return s;
}

// Fieldstat_tag_wrapper::Fieldstat_tag_wrapper(const char *key, long long value) {
//     tag.key = key;
//     tag.type = TAG_INTEGER;
//     tag.value_int = value;
// }

// Fieldstat_tag_wrapper::Fieldstat_tag_wrapper(const char *key, double value) {
//     tag.key = key;
//     tag.type = TAG_DOUBLE;
//     tag.value_double = value;
// }

// Fieldstat_tag_wrapper::Fieldstat_tag_wrapper(const char *key, const char *value) {
//     tag.key = key;
//     tag.type = TAG_CSTRING;
//     tag.value_str = value;
//     // TODO: 这个地方应该是strdup。说实话这个类感觉都没什么用了，就先这样吧
// }

// Fieldstat_tag_wrapper::Fieldstat_tag_wrapper(const struct fieldstat_tag *tag) {
//     switch (tag->type)
//     {
//         case TAG_INTEGER:
//             Fieldstat_tag_wrapper(tag->key, tag->value_int);
//             break;
//         case TAG_DOUBLE:
//             Fieldstat_tag_wrapper(tag->key, tag->value_double);
//             break;
//         case TAG_CSTRING:
//             Fieldstat_tag_wrapper(tag->key, tag->value_str);
//             break;
//         default:
//             break;
//     }
// }

// Fieldstat_tag_wrapper::Fieldstat_tag_wrapper(const std::uniform_int_distribution<int>& dist)
// {
//     default_random_engine e;
//     tag.key = strdup(gen_rand_string(10).c_str());
//     int rand_ret = rand() % 3;
//     if (rand_ret == 0)
//     {
//         tag.type = TAG_INTEGER;
//         tag.value_int = static_cast<int>(dist(e));
//     }
//     else if (rand_ret == 1)
//     {
//         tag.type = TAG_DOUBLE;
//         tag.value_double = static_cast<int>(dist(e));
//     }
//     else
//     {
//         tag.type = TAG_CSTRING;
//         tag.value_str = strdup(gen_rand_string(10).c_str());
//     }
// }
// Fieldstat_tag_wrapper::~Fieldstat_tag_wrapper() {
//     if (tag.type == TAG_CSTRING)
//     {
//         free((void *)tag.value_str);
//     }
//     free((void *)tag.key);
// }

// bool Fieldstat_tag_wrapper::operator==(const Fieldstat_tag_wrapper& rhs) const {
//     if (tag.type != rhs.tag.type)
//     {
//         return false;
//     }
//     if (tag.type == TAG_INTEGER)
//     {
//         return tag.value_int == rhs.tag.value_int;
//     }
//     else if (tag.type == TAG_DOUBLE)
//     {
//         return tag.value_double == rhs.tag.value_double;
//     }
//     else
//     {
//         return strcmp(tag.value_str, rhs.tag.value_str) == 0;
//     }
//     if (strcmp(tag.key, rhs.tag.key) != 0) {
//         return false;
//     }
//     return true;
// }

// const struct fieldstat_tag& Fieldstat_tag_wrapper::get_c_struct() const
// {
//     return tag;
// }

/* -------------------------------------------------------------------------- */
/*                               taglist wrapper                              */
/* -------------------------------------------------------------------------- */

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(struct fieldstat_tag_list *tag_list) {
    tag_list_c.tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_list->n_tag);
    tag_list_c.n_tag = tag_list->n_tag;
    for (size_t i = 0; i < tag_list->n_tag; i++)
    {
        // copy the tag_list
        tag_list_c.tag[i].key = strdup(tag_list->tag[i].key);
        tag_list_c.tag[i].type = tag_list->tag[i].type;
        switch (tag_list->tag[i].type)
        {
            case TAG_INTEGER:
                tag_list_c.tag[i].value_int = tag_list->tag[i].value_int;
                break;
            case TAG_DOUBLE:
                tag_list_c.tag[i].value_double = tag_list->tag[i].value_double;
                break;
            case TAG_CSTRING:
                tag_list_c.tag[i].value_str = strdup(tag_list->tag[i].value_str);
                break;
            default:
                break;
        }
    }
}

Fieldstat_tag_list_wrapper::~Fieldstat_tag_list_wrapper() {
    for (size_t i = 0; i < tag_list_c.n_tag; i++)
    {
        free((void *)tag_list_c.tag[i].key);
        if (tag_list_c.tag[i].type == TAG_CSTRING)
        {
            free((void *)tag_list_c.tag[i].value_str);
        }
    }
    free((void *)tag_list_c.tag);
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(std::uniform_int_distribution<int> &dist)
{
    int tag_count = get_tag_count(); // TODO: 更多的tag数量产生方式
    tag_list_c.tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_count);
    tag_list_c.n_tag = tag_count;
    std::mt19937 rng(RAND_SEED);
    for (int i = 0; i < tag_count; i++)
    {
        tag_list_c.tag[i].key = strdup(gen_rand_string(10).c_str());
        int rand_ret = rand() % 3;
        if (rand_ret == 0)
        {
            tag_list_c.tag[i].type = TAG_INTEGER;
            tag_list_c.tag[i].value_int = static_cast<long long>(dist(rng));
        }
        else if (rand_ret == 1)
        {
            tag_list_c.tag[i].type = TAG_DOUBLE;
            tag_list_c.tag[i].value_double = static_cast<double>(dist(rng));
        }
        else
        {
            tag_list_c.tag[i].type = TAG_CSTRING;
            tag_list_c.tag[i].value_str = strdup(gen_rand_string(10).c_str());
        }
    }
}

const struct fieldstat_tag *Fieldstat_tag_list_wrapper::get_tag()
{
    return tag_list_c.tag;
}

size_t Fieldstat_tag_list_wrapper::get_tag_count()
{
    return tag_list_c.n_tag;
}


/* -------------------------------------------------------------------------- */
/*                                 Api_caller                                 */
/* -------------------------------------------------------------------------- */

template <typename ReturnType, typename... Args>
Api_caller<ReturnType, Args...>::Api_caller() : rng(std::random_device()()), distribution(1, 100)
{
}

template <typename ReturnType, typename... Args>
void Api_caller<ReturnType, Args...>::addApiFunction(const std::function<ReturnType(Args...)>& apiFunction) {
    apiFunctions.push_back(apiFunction);
}

template <typename ReturnType, typename... Args>
void Api_caller<ReturnType, Args...>::testApis()
{
    for (const auto& apiFunction : apiFunctions) {
        std::vector<std::tuple<Args...>> args = generateArguments();
        for (const auto& argSet : args) {
            ReturnType result = callApiFunction(apiFunction, argSet);
            recordInputOutput(apiFunction, argSet, result);
        }
    }
}

template <typename ReturnType, typename... Args>
void Api_caller<ReturnType, Args...>::testApis(int loop_num) {
    for (int i = 0; i < loop_num; i++) {
        testApis();
    }
}

template<typename ReturnType, typename... Args>
template<typename Arg>
Arg Api_caller<ReturnType, Args...>::generateArgument() {
    return static_cast<Arg>(distribution(rng));
}

template<typename ReturnType, typename... Args>
template<size_t Index>
void Api_caller<ReturnType, Args...>::generateArgumentsImpl(std::vector<std::tuple<Args...>>& args, std::tuple<Args...>& current) {
    if constexpr (Index == sizeof...(Args)) {
        args.push_back(current);
    } else {
        std::get<Index>(current) = generateArgument<std::tuple_element_t<Index, std::tuple<Args...>>>();
        generateArgumentsImpl<Index + 1, Args...>(args, current);
    }
}

template<typename ReturnType, typename... Args>
template<size_t Index>
void Api_caller<ReturnType, Args...>::generateArgumentsImpl(std::vector<std::tuple<Args...>>& args) {
    std::tuple<Args...> current;
    generateArgumentsImpl<Index, Args...>(args, current);
}

template<typename ReturnType, typename... Args>
std::vector<std::tuple<Args...>> Api_caller<ReturnType, Args...>::generateArguments() {
    std::vector<std::tuple<Args...>> args;
    generateArgumentsImpl<0, Args...>(args);
    return args;
}

// Call an API function with arguments
template<typename ReturnType, typename... Args>
ReturnType Api_caller<ReturnType, Args...>::callApiFunction(const std::function<ReturnType(Args...)>& apiFunction, const std::tuple<Args...>& args) {
    return std::apply(apiFunction, args);
}

// TODO: 看看到底有没用，没用就删了吧
template<size_t Index, typename... Args>
void printInput(const std::tuple<Args...>& args) {
    if constexpr (Index < sizeof...(Args)) {
        std::cout << std::get<Index>(args) << " ";
        printInput<Index + 1, Args...>(args);
    }
}

// Record the input arguments and output of an API function
template<typename ReturnType, typename... Args>
void Api_caller<ReturnType, Args...>::recordInputOutput(Args... args, ReturnType result)
{
    arguments.emplace_back(std::make_tuple(args...));
    results.push_back(result);
}

template<typename ReturnType, typename... Args>
std::vector<std::tuple<Args...>>Api_caller<ReturnType, Args...>::getArguments()
{
    return arguments;
}

template <typename Query_ret>
Query_caller<Query_ret>::Query_caller(const std::function<Query_ret(Fieldstat_tag_list_wrapper &)>& queryFunction) : queryFunction(queryFunction)
{
}

template <typename Query_ret>
template <typename... Args>
void Query_caller<Query_ret>::take_args(std::vector<std::tuple<Args...>>& args)
{
    for (auto& arg : args)
    {
        extract_tags(arg);
    }
}

template <typename Query_ret>
std::vector<Query_ret> Query_caller<Query_ret>::get_results()
{
    std::vector<Query_ret> results;
    std::transform(arguments.begin(), arguments.end(), std::back_inserter(results), queryFunction);
    return results;
}

template <typename Query_ret>
template <typename... Args>
void Query_caller<Query_ret>::extract_tags(std::tuple<Args...>& args)
{
    Fieldstat_tag_list_wrapper tag_list_wrapper;
    arguments.push_back(std::get<Fieldstat_tag_list_wrapper>(args));
}

template <typename Query_ret>
double cal_accuracy(std::vector<Query_ret>& results, std::vector<Query_ret>& expected_results)
{
    int count = 0;

    for (const auto &expected_result : expected_results) {
        count += std::count(results.begin(), results.end(), expected_result);
    }

    return count / results.size();
}