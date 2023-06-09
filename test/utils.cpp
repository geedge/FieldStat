

#include <string>
#include <stdlib.h>


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