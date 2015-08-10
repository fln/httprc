#pragma once

#include <stddef.h>

struct result {
	const char *id;
        int     return_code;
        char   *stdout_buffer;
        size_t  stdout_size;
        char   *stderr_buffer;
        size_t  stderr_size;
	int     duration_ms;
};

struct result *result_new(size_t buf_size);
void result_print(struct result *res);
void result_free(struct result *res);
