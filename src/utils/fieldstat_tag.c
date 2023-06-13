// #include <assert.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <string.h>

// void tag_pair_unzip(const struct fieldstat_tag *tag, char **tag_key_out, char **tag_value_out)
// {
//     *tag_key_out = strdup(tag->key);
//     char str_double[sizeof(double) + 1];
//     char str_longlong[sizeof(long long) + 1];
//     switch(tag->type)
//     {
//         case TAG_INTEGER:
//             memset(str_longlong, 0, sizeof(str_longlong));
//             snprintf(str_longlong, sizeof(str_longlong), "%lld", tag->value_int);
//             *tag_value_out = strdup((const char *)str_longlong);
//             break;
//         case TAG_DOUBLE:
//             memset(str_double, 0, sizeof(str_double));
//             snprintf(str_double, sizeof(str_double), "%lf", tag->value_double);
//             *tag_value_out = strdup((const char *)str_double);
//             break;
//         case TAG_CSTRING:
//             *tag_value_out = strdup(tag->value_str);
//             break;
//         default:
//             assert(0);
//             break;
//     }
// }


