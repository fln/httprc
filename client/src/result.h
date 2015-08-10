#pragma once

#include <stddef.h>

struct result {
        int     return_code;
        char   *stdout_buffer;
        size_t  stdout_size;
        char   *stderr_buffer;
        size_t  stderr_size;
};

struct result *result_new(size_t buf_size);
void result_free(struct result *res);
