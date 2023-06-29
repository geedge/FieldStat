

#include <gtest/gtest.h>
#include <stdio.h>

void copy_array_skipping_one_element(unsigned int *dst, unsigned int *src, size_t src_len, size_t skip_index) {
    if (skip_index != 0) {
        memcpy(dst, src, skip_index * sizeof(unsigned int));
    }
    if (skip_index != src_len - 1) {
        memcpy(dst + skip_index, src + skip_index + 1, (src_len - skip_index - 1) * sizeof(unsigned int));
    }
}

int main(int argc, char *argv[]) 
{
	unsigned int src[] = {1, 2, 3, 4, 5};
    unsigned int dst[4];
    copy_array_skipping_one_element(dst, src, 5, 2);

    for (int i = 0; i < 4; i++) {
        printf("%u ", dst[i]);
    }
    printf("\n");

    copy_array_skipping_one_element(dst, src, 5, 0);
    for (int i = 0; i < 4; i++) {
        printf("%u ", dst[i]);
    }
    printf("\n");

    copy_array_skipping_one_element(dst, src, 5, 4);
    for (int i = 0; i < 4; i++) {
        printf("%u ", dst[i]);
    }
    printf("\n");
}