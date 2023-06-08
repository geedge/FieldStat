#pragma once
#include <stdio.h>
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
int fieldstat_enable_prometheus_output(struct fieldstat_instance *instance);
/**
 * Enable output metric to statsd server.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param ip  Statsd server ip.
 * @param port Statsd server port. i.e., 80,8080
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_statsd_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to line protocol server.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param ip  Line protocol server ip.
 * @param port Line protocol server port. i.e., 80,8080
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_line_protocol_server(struct fieldstat_instance *instance, const char *ip, unsigned short port);
/**
 * Enable output metric to file.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @param filename The output target filename.
 * @param format The output format. value in {"json","default"}
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_local_output(struct fieldstat_instance *instance, const char *filename, const char *format);
/**
 * Disable the background thread.
 * The operation should be executed in single-threaded fashion.
 * @param instance The fieldstat instance.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_disable_background_thread(struct fieldstat_instance *instance);
/**
 * Set the output interval in milliseconds
 * The operation should be executed in single-threaded fashion.
 * @param seconds  In milliseconds.
 * @return -1 is failed. 0 is success.
 */
int fieldstat_set_output_interval(struct fieldstat_instance *instance, int milliseconds);

#ifdef __cplusplus
}
#endif