#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

#include "fieldstat.h"
#include "fieldstat_exporter.h"
#include "fieldstat_easy.h"

struct fs_unit {
    struct fieldstat *active;
    struct fieldstat *read_only;
    pthread_spinlock_t lock;
};

static volatile int g_output_thread_running = 0;


void close_output_thread()
{
    (void)__sync_lock_test_and_set(&g_output_thread_running, 0);
}

struct fieldstat_easy
{
    struct fs_unit *fsu;
    int max_thread_num;

    struct fieldstat *delta;
    struct fieldstat *accumulate;
    pthread_t output_thread;
    struct fieldstat_json_exporter *exporter;
    FILE *output_fp;
    int output_interval_second;
    pthread_spinlock_t outputting_lock; // lock the resource: fieldstat_easy::accumulate
};

void fs_unit_switch_role(struct fs_unit *fsu) {
    fieldstat_reset(fsu->read_only);

    pthread_spin_lock(&fsu->lock);
    struct fieldstat *tmp = fsu->active;
    fsu->active = fsu->read_only;
    fsu->read_only = tmp;
    pthread_spin_unlock(&fsu->lock);
}

char *output_work(struct fieldstat_easy *fs, const struct timeval *timestamp, const struct timeval *timestamp_delta)
{
    fieldstat_reset(fs->delta);

    for (int i = 0; i < fs->max_thread_num; i++) {
        fs_unit_switch_role(fs->fsu + i);
        fieldstat_merge(fs->delta, fs->fsu[i].read_only);
    }
    pthread_spin_lock(&fs->outputting_lock);
    fieldstat_merge(fs->accumulate, fs->delta);
    char *ret = fieldstat_json_exporter_export_with_delta(fs->exporter, fs->accumulate, fs->delta, timestamp, timestamp_delta);
    pthread_spin_unlock(&fs->outputting_lock);

    return ret;
}

void *output_main(void *arg) // return void * for pthread_create check only
{
    struct timespec entry_time;
    clock_gettime(CLOCK_MONOTONIC, &entry_time);
    long long last_run_time = entry_time.tv_sec * 1000 + entry_time.tv_nsec / 1000000;
    // long long last_run_time = 0;
    struct timeval timestamp;
    struct timeval timestamp_delta;
    struct timespec this_output_time;
    struct fieldstat_easy *fs = (struct fieldstat_easy *)arg;
    long long output_interval = fs->output_interval_second * 1000;

    while (g_output_thread_running) {
        clock_gettime(CLOCK_MONOTONIC, &this_output_time);
        long long now = this_output_time.tv_sec * 1000 + this_output_time.tv_nsec / 1000000;
        if (now - last_run_time < output_interval) {
            usleep(50000); // 50ms
            continue;
        }

        timestamp.tv_sec = this_output_time.tv_sec;
        timestamp.tv_usec = this_output_time.tv_nsec / 1000;
        timestamp_delta.tv_sec = timestamp.tv_sec - last_run_time / 1000; // divide 1000 to convert ms to sec
        timestamp_delta.tv_usec = timestamp.tv_usec - (last_run_time % 1000) * 1000; // %1000 to get the ms part, then *1000 to convert ms to us
        last_run_time = now;

        char *ret = output_work(fs, &timestamp, &timestamp_delta);
        if (ret == NULL) {
            ret = strdup("[]");
        }
        struct flock lock;
        lock.l_type = F_WRLCK;
        lock.l_start = 0;
        lock.l_whence = SEEK_SET;
        lock.l_len = 0;
        if (fcntl(fileno(fs->output_fp), F_SETLKW, &lock) < 0) {
            fprintf(stderr, "output_main: fcntl failed to lock\n");
            free(ret);
            continue;
        }

        // clear file and write
        ftruncate(fileno(fs->output_fp), 0);
        rewind(fs->output_fp);
        fprintf(fs->output_fp, "%s\n", ret);

        lock.l_type = F_UNLCK;
        if (fcntl(fileno(fs->output_fp), F_SETLK, &lock) < 0) {
            fprintf(stderr, "output_main: fcntl failed to release\n");
        }

        free(ret);
    }
    return NULL; // return void * for pthread_create check only
}

struct fieldstat_easy *fieldstat_easy_new(int max_thread_num, const struct fieldstat_tag *tags, size_t n_tag) {
    struct fieldstat_easy *fse = calloc(1, sizeof(struct fieldstat_easy));
    fse->fsu = malloc(sizeof(struct fs_unit) * max_thread_num);
    fse->max_thread_num = max_thread_num;
    fse->delta = fieldstat_new();
    fieldstat_create_cube(fse->delta, NULL, 0, SAMPLING_MODE_COMPREHENSIVE, 0);

    fse->accumulate = fieldstat_fork(fse->delta);
    fse->exporter = fieldstat_json_exporter_new();
    fieldstat_json_exporter_set_global_tag(fse->exporter, tags, n_tag);
    pthread_spin_init(&fse->outputting_lock, PTHREAD_PROCESS_PRIVATE);

    for (int i = 0; i < max_thread_num; i++) {
        fse->fsu[i].active = fieldstat_fork(fse->delta);
        fse->fsu[i].read_only = fieldstat_fork(fse->delta);
        pthread_spin_init(&fse->fsu[i].lock, PTHREAD_PROCESS_PRIVATE);
    }

    return fse;
}

void fieldstat_easy_free(struct fieldstat_easy *fse) {
    if (g_output_thread_running) {
        close_output_thread();
        pthread_join(fse->output_thread, NULL);
        fclose(fse->output_fp);
    }

    pthread_spin_destroy(&fse->outputting_lock);
    fieldstat_free(fse->delta);
    fieldstat_free(fse->accumulate);
    fieldstat_json_exporter_free(fse->exporter);

    for (int i = 0; i < fse->max_thread_num; i++) {
        pthread_spin_lock(&fse->fsu[i].lock);
        fieldstat_free(fse->fsu[i].active);
        fieldstat_free(fse->fsu[i].read_only);
        pthread_spin_unlock(&fse->fsu[i].lock);
        pthread_spin_destroy(&fse->fsu[i].lock);
    }
    free(fse->fsu);

    free(fse);
}


int fieldstat_easy_enable_auto_output(struct fieldstat_easy *pthis, const char *output_path, int interval_second) {
    if (g_output_thread_running) {
        return -2;
    }

    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        return -1;
    }

    pthis->output_fp = fp;
    g_output_thread_running = 1;
    pthis->output_interval_second = interval_second;
    pthread_create(&pthis->output_thread, NULL, output_main, pthis);

    return 0;
}

int fieldstat_easy_register_counter(struct fieldstat_easy *fse, const char *name) {
    for (int i = 0; i < fse->max_thread_num; i++) {
        pthread_spin_lock(&fse->fsu[i].lock);
    }

    int ret = fieldstat_register_counter(fse->fsu[0].active, name); // try to register
    if (ret < 0) {
        for (int i = 0; i < fse->max_thread_num; i++) {
            pthread_spin_unlock(&fse->fsu[i].lock);
        }
        return ret;
    }
    fieldstat_register_counter(fse->fsu[0].read_only, name);
    for (int i = 1; i < fse->max_thread_num; i++) {
        fieldstat_register_counter(fse->fsu[i].active, name);
        fieldstat_register_counter(fse->fsu[i].read_only, name);
    }

    for (int i = 0; i < fse->max_thread_num; i++) {
        pthread_spin_unlock(&fse->fsu[i].lock);
    }

    return ret;
}

int fieldstat_easy_register_histogram(struct fieldstat_easy *fse, const char *name, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
{
    for (int i = 0; i < fse->max_thread_num; i++) {
        pthread_spin_lock(&fse->fsu[i].lock);
    }

    int ret = fieldstat_register_hist(fse->fsu[0].active, name, lowest_trackable_value, highest_trackable_value, significant_figures); // try to register
    if (ret < 0) {
        for (int i = 0; i < fse->max_thread_num; i++) {
            pthread_spin_unlock(&fse->fsu[i].lock);
        }
        return ret;
    }
    fieldstat_register_hist(fse->fsu[0].read_only, name, lowest_trackable_value, highest_trackable_value, significant_figures);
    for (int i = 1; i < fse->max_thread_num; i++) {
        fieldstat_register_hist(fse->fsu[i].active, name, lowest_trackable_value, highest_trackable_value, significant_figures);
        fieldstat_register_hist(fse->fsu[i].read_only, name, lowest_trackable_value, highest_trackable_value, significant_figures);
    }

    for (int i = 0; i < fse->max_thread_num; i++) {
        pthread_spin_unlock(&fse->fsu[i].lock);
    }

    return ret;
}

// output an json string for accumulated data
void fieldstat_easy_output(struct fieldstat_easy *fse, char **buff, size_t *buff_len)
{
    struct timeval timestamp;
    struct timespec this_output_time;
    clock_gettime(CLOCK_MONOTONIC, &this_output_time); // use the same method as output_main
    timestamp.tv_sec = this_output_time.tv_sec;
    timestamp.tv_usec = this_output_time.tv_nsec / 1000;
    struct fieldstat *dst = fieldstat_new();
    // collect all the data recorded since last passive output, if passive output happened. Otherwise, its the data since the program started.
    for (int i = 0; i < fse->max_thread_num; i++) {
        pthread_spin_lock(&fse->fsu[i].lock);
        fieldstat_merge(dst, fse->fsu[i].active);
        pthread_spin_unlock(&fse->fsu[i].lock);
    }

    // add the outputted data
    pthread_spin_lock(&fse->outputting_lock);
    fieldstat_merge(dst, fse->accumulate);
    pthread_spin_unlock(&fse->outputting_lock);

    *buff = fieldstat_json_exporter_export(fse->exporter, dst, &timestamp);
    if (*buff == NULL) {
        *buff = strdup("[]");
    }
    fieldstat_free(dst);
    *buff_len = strlen(*buff);
}

int fieldstat_easy_counter_incrby(struct fieldstat_easy *fse, int thread_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long increment)
{
    if (thread_id < 0) {
        return -1;
    }
    if (thread_id >= fse->max_thread_num) {
        return -1;
    }

    pthread_spin_lock(&fse->fsu[thread_id].lock);
    int ret = fieldstat_counter_incrby(fse->fsu[thread_id].active, 0, metric_id, tags, n_tag, increment);
    pthread_spin_unlock(&fse->fsu[thread_id].lock);

    return ret;
}

int fieldstat_easy_histogram_record(struct fieldstat_easy *fse, int thread_id, int metric_id, const struct fieldstat_tag *tags, size_t n_tag, long long value)
{
    if (thread_id < 0) {
        return -1;
    }
    if (thread_id >= fse->max_thread_num) {
        return -1;
    }

    pthread_spin_lock(&fse->fsu[thread_id].lock);
    int ret = fieldstat_hist_record(fse->fsu[thread_id].active, 0, metric_id, tags, n_tag, value);
    pthread_spin_unlock(&fse->fsu[thread_id].lock);

    return ret;
}
