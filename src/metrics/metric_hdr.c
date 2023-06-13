// // #include <assert.h>
// // #include <stdlib.h>
// // #include <stdio.h>
// // #include <string.h>

// // #include "metrics/metric_hdr.h"
// // #include "output/serializer.h"
// // #include "cjson/cJSON.h"
// #include "metric_type_internal.h"

// #include "field_and_metric.h"

// struct metric_hdr {
//     struct cell base;
//     struct hdr_histogram *hdr;
//     double *bins;
// };

// // void metric_hdr_serialization(const struct metric_hdr *hdr, char **blob, size_t *blob_sz)
// // {
// //     struct hdr_histogram *h = hdr->hdr;
// //     struct hdr_iter iter;

// //     cJSON *root = cJSON_CreateObject();
// //     cJSON_AddStringToObject(root, "type", "hdr");
// //     cJSON_AddNumberToObject(root, "ltv", h->lowest_trackable_value);
// //     cJSON_AddNumberToObject(root, "htv", h->highest_trackable_value);
// //     cJSON_AddNumberToObject(root, "sf", h->significant_figures);

// //     hdr_iter_init(&iter, h);
// //     cJSON *hist = cJSON_CreateArray();
// //     while (hdr_iter_next(&iter)) {
// //         cJSON *y_json = cJSON_CreateNumber(iter.count);
// //         printf("count %ld at index: %d\n", iter.count, iter.counts_index);
// //         cJSON_AddItemToArray(hist, y_json);
// //     }
// //     cJSON_AddItemToObject(root, "hist", hist);

// //     char *out = cJSON_PrintUnformatted(root);
// //     *blob = out;
// //     *blob_sz = strlen(out) + 1;
// //     cJSON_Delete(root);
// // }

// // int metric_hdr_dump(struct cell *pthis, struct serializer *serializer)
// // {
// //     struct metric_hdr *hdr = (struct metric_hdr *)pthis;

// //     switch (serializer->type) {
// //         case SERIALIZER_TYPE_BIN:
// //         {
// //             struct serializer_bin *bin = (struct serializer_bin *)serializer;

// //             metric_hdr_serialization(hdr, &bin->buf[bin->buf_num], &bin->buf_size[bin->buf_num]);
// //             bin->field_name[bin->buf_num] = strdup(hdr->base.name);
// //             bin->buf_num++;
// //             return 0;
// //             break;
// //         }
// //         default:
// //             assert(0);
// //             break;
// //     }
// //     return -1;
// // }

// // void metric_hdr_refresh(struct cell *pthis)
// // {
// //     struct metric_hdr *hdr = (struct metric_hdr *)pthis;
// //     hdr_reset(hdr->hdr);
// // }

// // void metric_hdr_del(struct cell *pthis)
// // {
// //     struct metric_hdr *hdr = (struct metric_hdr *)pthis;
// //     free(pthis->name);
// //     hdr_close(hdr->hdr);
// //     free(hdr->bins);
// //     free(hdr);
// // }

// // int metric_hdr_merge(struct cell *pthis, struct cell *from)
// // {
// //     struct metric_hdr *hdr = (struct metric_hdr *)pthis;
// //     struct metric_hdr *hdr_from = (struct metric_hdr *)from;
// //     uint64_t drop = hdr_add(hdr->hdr, hdr_from->hdr);
// //     return drop == 0 ? 0 : -1;
// // }

// // static int parse_histogram_bin_format(const char* format, double **output_bins)
// // {
// // 	char *token=NULL,*sub_token=NULL,*saveptr;
// // 	size_t i=0;
// // 	int comma_num=0,ret=0;
// // 	double *bins;
// // 	char* dup_format=strdup(format);
// // 	for(i=0;i<strlen(dup_format);i++)
// // 	{
// // 		if(dup_format[i]==',')
// // 		{
// // 			comma_num++;
// // 		}
// // 	}
// // 	bins=(double*)calloc(comma_num+1, sizeof(double));
// // 	for (token = dup_format,i=0; ; token= NULL, i++)
// // 	{
// // 		sub_token= strtok_r(token,",", &saveptr);
// // 		if (sub_token == NULL)
// // 			break;
// // 		ret=sscanf(sub_token,"%lf",bins+i);

// // 		if(ret!=1)
// // 		{
// // 			goto error_out;
// // 		}
// // 	}
// // 	free(dup_format);
// // 	*output_bins=bins;
// // 	return i;
// // error_out:
// // 	free(dup_format);
// // 	free(bins);
// // 	return -1;
// // }

// // struct metric_hdr *metric_hdr_create(const char *name, size_t name_len,
// //     const char * bins, long long lowest_trackable_value, long long highest_trackable_value, int significant_figures)
// // {
// //     if (lowest_trackable_value < 1 || significant_figures < 1 || 5 < significant_figures)
// // 	{
// // 		return NULL;
// // 	}

// // 	if (lowest_trackable_value * 2 > highest_trackable_value)
// // 	{
// // 		return NULL;
// // 	}

// //     double *bin_double = NULL;
// //     int ret = parse_histogram_bin_format((const char*)bins, &bin_double);
// //     if (ret < 0) {
// //         return NULL;
// //     }

// //     struct hdr_histogram* tmp_p_hdr = NULL;
// // 	ret = hdr_init((int64_t)lowest_trackable_value, (int64_t)highest_trackable_value, significant_figures, &tmp_p_hdr);
// // 	if (ret != 0) {
// //         free(bin_double);
// //         return NULL;
// //     }

// //     struct metric_hdr *hdr = malloc(sizeof(struct metric_hdr));
// //     hdr->bins = bin_double;
// //     hdr->hdr = tmp_p_hdr;

// //     struct cell *base = (struct cell *)hdr;
// //     base->type = FIELD_TYPE_HISTOGRAM;
// //     base->name = strdup(name);
// //     base->dump = metric_hdr_dump;
// //     base->refresh = metric_hdr_refresh;
// //     base->del = metric_hdr_del;
// //     base->merge = metric_hdr_merge;

// //     return hdr;
// // }

// // int metric_hdr_add(struct metric_hdr *unique, long long value)
// // {
// //     struct hdr_histogram *h = unique->hdr;
// //     bool ret = hdr_record_value(h, value);
// //     if (ret == false) {
// //         return -1;
// //     }
// //     return 0;
// // }