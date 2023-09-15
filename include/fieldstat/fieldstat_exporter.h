#pragma once
#include <stdio.h>
#include <time.h>
#include "fieldstat.h"

#ifdef __cplusplus
extern "C"
{
#endif
/**
 * Create prometheus endpoint. 
 * The operation should be executed in single-threaded fashion.
 * @param listen_port The endpoint listen port. i.e., 80,8080
 * @param url_path The endpoint url path. url path is "/metrics" when url path is NULL.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_global_enable_prometheus_endpoint(unsigned short listen_port, const char *url_path);
/**
 * Enable fieldstat instance output prometheus
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_enable_prometheus_output(struct fieldstat *instance);
/**
 * Enable output metric to statsd server.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param ip  Statsd server ip.
 * @param port Statsd server port. i.e., 80,8080
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_statsd_server(struct fieldstat *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to line protocol server.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param ip  Line protocol server ip.
 * @param port Line protocol server port. i.e., 80,8080
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_line_protocol_server(struct fieldstat *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to file.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param filename The output target filename.
 * @param format The output format. value in {"json","default"}
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_local_output(struct fieldstat *instance, const char *filename, const char *format);
/**
 * Disable the background thread.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_disable_background_thread(struct fieldstat *instance);
/**
 * Set the output interval in milliseconds
 * The operation should be executed in single-threaded fashion.
 * @param seconds  In milliseconds.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_output_interval(struct fieldstat *instance, int milliseconds);


/* -------------------------------------------------------------------------- */
/*                           fieldstat_json_exporter                          */
/* -------------------------------------------------------------------------- */
struct fieldstat_json_exporter;

struct fieldstat_json_exporter *fieldstat_json_exporter_new(const struct fieldstat *instance);
void fieldstat_json_exporter_set_global_tag(struct fieldstat_json_exporter *exporter, const struct fieldstat_tag tag_list[], size_t n_tag);
void fieldstat_json_exporter_set_name(struct fieldstat_json_exporter *exporter, const char *name);
void fieldstat_json_exporter_free(struct fieldstat_json_exporter *exporter);

/*
    Output the fieldstat instance to json string array. User must free the output string.
*/
char *fieldstat_json_exporter_export(const struct fieldstat_json_exporter *exporter, const struct timeval *timestamp);
/*
    after call fieldstat_json_exporter_export_array, user must free the output array and each string.
*/
void fieldstat_json_exporter_export_array(const struct fieldstat_json_exporter *exporter, const struct timeval *timestamp, char ***output, size_t *output_size);

/*
    let json exporter output delta value by the side of the original value(accumulated). If a cell / metric is new, the delta value is the same as the original value.
    Outputting delta value is disabled by default.
    When the exporter name or exporter global tags are changed, or the fieldstat instance is reset, or one of the cube is deleted, the delta value will be reset. (next output will be the original value)
    it is recommended to call this function after fieldstat_json_exporter_set_name and fieldstat_json_exporter_set_global_tag.
    Only affects the metrics of counter type.
    Outputting delta value is time-consuming.
*/
void fieldstat_json_exporter_enable_delta(struct fieldstat_json_exporter *exporter);

#ifdef __cplusplus
}
#endif