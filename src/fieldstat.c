#include "fieldstat/fieldstat.h"
#include "uthash.h"
struct fs_metric
{
    
};
struct fs_field
{
    int field_id;
    enum field_type type;
    struct fs_metric table;// tag -> metric
};
struct fieldstat
{
    size_t n_field;
    struct fs_field *field_array;
};