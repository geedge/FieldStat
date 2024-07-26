#include <string>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <set>
#include <unordered_map>    
#include <random>
#include <string.h>
#include <algorithm>
#include <fstream>
#include <math.h>
#include <unistd.h>
#include <sstream>

#include "fieldstat.h"
#include "utils.hpp"

using namespace std;


string gen_rand_string(int len)
{
    char cstr[len + 1];
    for (int i = 0; i < len; i++)
    {
        cstr[i] = 'a' + rand() % 26;
    }
    cstr[len] = '\0';
    string s(cstr);
    return s;
}

/* -------------------------------------------------------------------------- */
/*                               taglist wrapper                              */
/* -------------------------------------------------------------------------- */

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(const struct field_list *tag_list) {
    tag_list_c.field = (struct field *)malloc(sizeof(struct field) * tag_list->n_field);
    tag_list_c.n_field = tag_list->n_field;
    for (size_t i = 0; i < tag_list->n_field; i++)
    {
        // copy the tag_list
        tag_list_c.field[i].key = strdup(tag_list->field[i].key);
        tag_list_c.field[i].type = tag_list->field[i].type;
        switch (tag_list->field[i].type)
        {
            case FIELD_VALUE_INTEGER:
                tag_list_c.field[i].value_longlong = tag_list->field[i].value_longlong;
                break;
            case FIELD_VALUE_DOUBLE:
                tag_list_c.field[i].value_double = tag_list->field[i].value_double;
                break;
            case FIELD_VALUE_CSTRING:
                tag_list_c.field[i].value_str = strdup(tag_list->field[i].value_str);
                break;
            default:
                break;
        }
    }
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(const char * key, int value)
{
    tag_list_c.field = (struct field *)malloc(sizeof(struct field));
    tag_list_c.n_field = 1;
    tag_list_c.field[0].key = strdup(key);
    tag_list_c.field[0].type = FIELD_VALUE_INTEGER;
    tag_list_c.field[0].value_longlong = value;
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(const char * key, const char *value)
{
    tag_list_c.field = (struct field *)malloc(sizeof(struct field));
    tag_list_c.n_field = 1;
    tag_list_c.field[0].key = strdup(key);
    tag_list_c.field[0].type = FIELD_VALUE_CSTRING;
    tag_list_c.field[0].value_str = strdup(value);
}


Fieldstat_tag_list_wrapper::~Fieldstat_tag_list_wrapper() {
    for (size_t i = 0; i < tag_list_c.n_field; i++) {
        free((char *)tag_list_c.field[i].key);
        if (tag_list_c.field[i].type == FIELD_VALUE_CSTRING) {
            free((char *)tag_list_c.field[i].value_str);
        }
    }
    free(tag_list_c.field);
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(std::uniform_int_distribution<int> &dist, int tag_count)
{
    tag_list_c.field = (struct field *)malloc(sizeof(struct field) * tag_count);
    tag_list_c.n_field = tag_count;
    std::mt19937 rng(1);
    for (int i = 0; i < tag_count; i++)
    {
        tag_list_c.field[i].key = strdup(gen_rand_string(10).c_str());
        int rand_ret = rand() % 3;
        if (rand_ret == 0)
        {
            tag_list_c.field[i].type = FIELD_VALUE_INTEGER;
            tag_list_c.field[i].value_longlong = static_cast<long long>(dist(rng));
        }
        else if (rand_ret == 1)
        {
            tag_list_c.field[i].type = FIELD_VALUE_DOUBLE;
            tag_list_c.field[i].value_double = static_cast<double>(dist(rng)) + 0.5;
        }
        else
        {
            tag_list_c.field[i].type = FIELD_VALUE_CSTRING;
            tag_list_c.field[i].value_str = strdup(gen_rand_string(10).c_str());
        }
    }
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper() {
    tag_list_c.field = NULL;
    tag_list_c.n_field = 0;
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(const Fieldstat_tag_list_wrapper &tag_list_wrapper){
    const struct field_list *tag_list = tag_list_wrapper.get_c_struct();
    tag_list_c.field = (struct field *)malloc(sizeof(struct field) * tag_list->n_field);
    tag_list_c.n_field = tag_list->n_field;
    for (size_t i = 0; i < tag_list->n_field; i++)
    {
        // copy the tag_list
        tag_list_c.field[i].key = strdup(tag_list->field[i].key);
        tag_list_c.field[i].type = tag_list->field[i].type;
        switch (tag_list->field[i].type)
        {
            case FIELD_VALUE_INTEGER:
                tag_list_c.field[i].value_longlong = tag_list->field[i].value_longlong;
                break;
            case FIELD_VALUE_DOUBLE:
                tag_list_c.field[i].value_double = tag_list->field[i].value_double;
                break;
            case FIELD_VALUE_CSTRING:
                tag_list_c.field[i].value_str = strdup(tag_list->field[i].value_str);
                break;
            default:
                break;
        }
    }
}

const struct field *Fieldstat_tag_list_wrapper::get_tag() const
{
    return tag_list_c.field;
}

size_t Fieldstat_tag_list_wrapper::get_tag_count() const
{
    return tag_list_c.n_field;
}

const struct field_list *Fieldstat_tag_list_wrapper::get_c_struct() const
{
    return &tag_list_c;
}

void Fieldstat_tag_list_wrapper::print_tag_list() const
{
    printf("tag_list_c.n_field: %zu\n", tag_list_c.n_field);
    for (size_t i = 0; i < tag_list_c.n_field; i++)
    {
        printf("tag_list_c.field[%zu].key: %s\n", i, tag_list_c.field[i].key);
        printf("tag_list_c.field[%zu].type: %d\n", i, (int)tag_list_c.field[i].type);
        switch (tag_list_c.field[i].type)
        {
            case FIELD_VALUE_INTEGER:
                printf("tag_list_c.field[%zu].value_longlong: %lld\n", i, tag_list_c.field[i].value_longlong);
                break;
            case FIELD_VALUE_DOUBLE:
                printf("tag_list_c.field[%zu].value_double: %lf\n", i, tag_list_c.field[i].value_double);
                break;
            case FIELD_VALUE_CSTRING:
                printf("tag_list_c.field[%zu].value_str: %s\n", i, tag_list_c.field[i].value_str);
                break;
            default:
                break;
        }
    }
    printf("print end\n");
}

string Fieldstat_tag_list_wrapper::to_string() const
{
    string str = "";
    for (size_t i = 0; i < tag_list_c.n_field; i++)
    {
        str += tag_list_c.field[i].key;
        str += ":";
        switch (tag_list_c.field[i].type)
        {
            case FIELD_VALUE_INTEGER:
                str += std::to_string(tag_list_c.field[i].value_longlong);
                break;
            case FIELD_VALUE_DOUBLE:
                str += std::to_string(tag_list_c.field[i].value_double);
                break;
            case FIELD_VALUE_CSTRING:
                str += tag_list_c.field[i].value_str;
                break;
            default:
                break;
        }
        str += ",";
    }
    return str;
}

bool Fieldstat_tag_list_wrapper::operator==(const Fieldstat_tag_list_wrapper &tag_list_wrapper) const
{
    const struct field_list *tag_list = tag_list_wrapper.get_c_struct();
    if (tag_list_c.n_field != tag_list->n_field) {
        return false;
    }
    for (size_t i = 0; i < tag_list_c.n_field; i++) {
        if (strcmp((char *)tag_list_c.field[i].key, (char *)tag_list->field[i].key) != 0) {
            return false;
        }
        if (tag_list_c.field[i].type != tag_list->field[i].type) {
            return false;
        }
        switch (tag_list_c.field[i].type) {
            case FIELD_VALUE_INTEGER:
                if (tag_list_c.field[i].value_longlong != tag_list->field[i].value_longlong) {
                    return false;
                }
                break;
            case FIELD_VALUE_DOUBLE:
                if (tag_list_c.field[i].value_double != tag_list->field[i].value_double) {
                    return false;
                }
                break;
            case FIELD_VALUE_CSTRING:
                if (strcmp((char *)tag_list_c.field[i].value_str, (char *)tag_list->field[i].value_str) != 0) {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}

Fieldstat_tag_list_wrapper& Fieldstat_tag_list_wrapper::sort_tag_list()
{
    std::sort(tag_list_c.field, tag_list_c.field + tag_list_c.n_field, [](const struct field &a, const struct field &b) {
        return strcmp((char *)a.key, (char *)b.key) < 0;
    });
    return *this;
}

double test_cal_topk_accuracy(vector<struct Fieldstat_tag_list_wrapper *> &test_result, unordered_map<string, int> &expected_count)
{
    std::vector<std::pair<std::string, int>> countVector(expected_count.begin(), expected_count.end());
    std::sort(countVector.begin(), countVector.end(), [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
        return a.second > b.second;
    });

    std::set<std::string> myset;
    int min_in_max_count = 0;
    size_t i;
    for (i = 0; i < test_result.size(); ++i) {
        myset.insert(countVector[i].first);
        min_in_max_count = countVector[i].second;
    }
    while (i < countVector.size()) {
        if (countVector[i].second != min_in_max_count) {
            break;
        }
        myset.insert(countVector[i].first);
        i++;
    }

    // cout << "myset : " << endl;
    // for (auto it = myset.begin(); it != myset.end(); it++) {
    //     cout << *it << endl;
    // }
    // cout << "------------------------- " << endl;

    int correct = 0;
    for (size_t i = 0; i < test_result.size(); i++) {
        string key = test_result[i]->to_string();
        if (myset.find(key) != myset.end()) {
            correct++;
        }
    }

    double accuracy = (double)correct / test_result.size();
    return accuracy;
}


//===========================================================================
//=  Function to generate Zipf (power law) distributed random variables     =
//=    - Input: alpha and N                                                 =
//=    - Output: Returns with Zipf distributed random variable              =
//===========================================================================
int zipf(double alpha, int n)
{
  static bool first = true;      // Static first time flag
  static double c = 0;          // Normalization constant
  double z;                     // Uniform random number (0 < z < 1)
  double sum_prob;              // Sum of probabilities
  double zipf_value;            // Computed exponential value to be returned
  int    i;                     // Loop counter

  // Compute normalization constant on first call only
  if (first)
  {
    for (i=1; i<=n; i++)
      c = c + (1.0 / pow((double) i, alpha));
    c = 1.0 / c;
    first = false;
  }

  // Pull a uniform random number (0 < z < 1)
  do
  {
    z = (double)rand() / (double)RAND_MAX;
  }
  while ((z == 0.0) || (z == 1.0));

  // Map z to the value
  sum_prob = 0;
  for (i=1; i<=n; i++)
  {
    sum_prob = sum_prob + c / pow((double) i, alpha);
    if (sum_prob >= z)
    {
      zipf_value = i;
      break;
    }
  }

  return(zipf_value);
}

SpreadSketchZipfGenerator::SpreadSketchZipfGenerator(double alpha, int n) { 
    _alpha = alpha;
    _n = n;
    cursor = 0;
    
    // generate data and write them to file
    std::string filename = "zipf_" + std::to_string(alpha) + "_" + std::to_string(n) + ".txt";

    std::unordered_map<int, int> fanout_map; // src_ip_id -> fanout being used

    if (access(filename.c_str(), F_OK) != 0) {
        printf("file %s not found, generating data\n", filename.c_str());

        std::ofstream file(filename);
        if (!file.is_open()) {
            printf("failed to open file %s\n", filename.c_str());
            return;
        }
        
        for (int i = 0; i < MAX_DATA; i++) {
            int src_id = zipf(alpha, n);
            int fanout = fanout_map.find(src_id) == fanout_map.end() ? 0 : fanout_map[src_id];
            fanout_map[src_id] = fanout + 1;

            file << "s_" << src_id << " d_" << fanout << std::endl;
        }
        
        file.close();
        printf("data generated and saved to file %s\n", filename.c_str());
    }

    // load data
    std::ifstream file(filename);
    if (!file.is_open()) {
        printf("failed to open file %s\n", filename.c_str());
        return;
    }

    loadeds = new std::vector<std::pair<std::string, std::string>>;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string src_ip, dst_ip;
        iss >> src_ip >> dst_ip;
        loadeds->push_back(std::make_pair(src_ip, dst_ip));
    }
    file.close();

}

SpreadSketchZipfGenerator::~SpreadSketchZipfGenerator() {
    delete loadeds;
}

struct Flow SpreadSketchZipfGenerator::next() {
    int r_cursor = cursor % loadeds->size();
    struct Flow flow;

    flow.src_ip = loadeds->at(r_cursor).first;
    flow.dst_ip = loadeds->at(r_cursor).second;

    cursor++;

    return flow;
}