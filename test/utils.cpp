#include <string>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <string.h>
#include <algorithm>


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

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(const struct fieldstat_tag_list *tag_list) {
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
                tag_list_c.tag[i].value_longlong = tag_list->tag[i].value_longlong;
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

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(const char * key, int value)
{
    tag_list_c.tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag));
    tag_list_c.n_tag = 1;
    tag_list_c.tag[0].key = strdup(key);
    tag_list_c.tag[0].type = TAG_INTEGER;
    tag_list_c.tag[0].value_longlong = value;
}

Fieldstat_tag_list_wrapper::~Fieldstat_tag_list_wrapper() {
    for (size_t i = 0; i < tag_list_c.n_tag; i++) {
        free((char *)tag_list_c.tag[i].key);
        if (tag_list_c.tag[i].type == TAG_CSTRING) {
            free((char *)tag_list_c.tag[i].value_str);
        }
    }
    free(tag_list_c.tag);
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(std::uniform_int_distribution<int> &dist, int tag_count)
{
    tag_list_c.tag = (struct fieldstat_tag *)malloc(sizeof(struct fieldstat_tag) * tag_count);
    tag_list_c.n_tag = tag_count;
    std::mt19937 rng(1);
    for (int i = 0; i < tag_count; i++)
    {
        tag_list_c.tag[i].key = strdup(gen_rand_string(10).c_str());
        int rand_ret = rand() % 3;
        if (rand_ret == 0)
        {
            tag_list_c.tag[i].type = TAG_INTEGER;
            tag_list_c.tag[i].value_longlong = static_cast<long long>(dist(rng));
        }
        else if (rand_ret == 1)
        {
            tag_list_c.tag[i].type = TAG_DOUBLE;
            tag_list_c.tag[i].value_double = static_cast<double>(dist(rng)) + 0.5;
        }
        else
        {
            tag_list_c.tag[i].type = TAG_CSTRING;
            tag_list_c.tag[i].value_str = strdup(gen_rand_string(10).c_str());
        }
    }
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper() {
    tag_list_c.tag = NULL;
    tag_list_c.n_tag = 0;
}

Fieldstat_tag_list_wrapper::Fieldstat_tag_list_wrapper(const Fieldstat_tag_list_wrapper &tag_list_wrapper){
    const struct fieldstat_tag_list *tag_list = tag_list_wrapper.get_c_struct();
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
                tag_list_c.tag[i].value_longlong = tag_list->tag[i].value_longlong;
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

const struct fieldstat_tag *Fieldstat_tag_list_wrapper::get_tag() const
{
    return tag_list_c.tag;
}

size_t Fieldstat_tag_list_wrapper::get_tag_count() const
{
    return tag_list_c.n_tag;
}

const struct fieldstat_tag_list *Fieldstat_tag_list_wrapper::get_c_struct() const
{
    return &tag_list_c;
}

void Fieldstat_tag_list_wrapper::print_tag_list() const
{
    printf("tag_list_c.n_tag: %zu\n", tag_list_c.n_tag);
    for (size_t i = 0; i < tag_list_c.n_tag; i++)
    {
        printf("tag_list_c.tag[%zu].key: %s\n", i, tag_list_c.tag[i].key);
        printf("tag_list_c.tag[%zu].type: %d\n", i, (int)tag_list_c.tag[i].type);
        switch (tag_list_c.tag[i].type)
        {
            case TAG_INTEGER:
                printf("tag_list_c.tag[%zu].value_longlong: %lld\n", i, tag_list_c.tag[i].value_longlong);
                break;
            case TAG_DOUBLE:
                printf("tag_list_c.tag[%zu].value_double: %lf\n", i, tag_list_c.tag[i].value_double);
                break;
            case TAG_CSTRING:
                printf("tag_list_c.tag[%zu].value_str: %s\n", i, tag_list_c.tag[i].value_str);
                break;
            default:
                break;
        }
    }
    printf("print end\n");
}

bool Fieldstat_tag_list_wrapper::operator==(const Fieldstat_tag_list_wrapper &tag_list_wrapper) const
{
    const struct fieldstat_tag_list *tag_list = tag_list_wrapper.get_c_struct();
    if (tag_list_c.n_tag != tag_list->n_tag) {
        return false;
    }
    for (size_t i = 0; i < tag_list_c.n_tag; i++) {
        if (strcmp((char *)tag_list_c.tag[i].key, (char *)tag_list->tag[i].key) != 0) {
            return false;
        }
        if (tag_list_c.tag[i].type != tag_list->tag[i].type) {
            return false;
        }
        switch (tag_list_c.tag[i].type) {
            case TAG_INTEGER:
                if (tag_list_c.tag[i].value_longlong != tag_list->tag[i].value_longlong) {
                    return false;
                }
                break;
            case TAG_DOUBLE:
                if (tag_list_c.tag[i].value_double != tag_list->tag[i].value_double) {
                    return false;
                }
                break;
            case TAG_CSTRING:
                if (strcmp((char *)tag_list_c.tag[i].value_str, (char *)tag_list->tag[i].value_str) != 0) {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}
