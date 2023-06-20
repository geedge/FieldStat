

#include <string>
#include <stdlib.h>
#include <iostream>
#include <type_traits>

#include "fieldstat.h"
#include "utils.hpp"

using namespace std;

string gen_rand_string(int len)
{
    string s;
    for (int i = 0; i < len; i++)
    {
        s += 'a' + rand() % 26;
    }
    return s;
}

// class Tester
// {
// public:
//     explicit Tester(string name)
//     {
//         string instance_name = name;
//         instance = fieldstat_new());
//     }
//     ~Tester()
//     {
//         fieldstat_free(instance);
//     }
//     template<typename First, typename... Args>
//     requires std::is_same_v<First, struct >

// private: 
//     string instance_name;
//     struct fieldstat *instance;
// };