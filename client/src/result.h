#pragma once

#include <stddef.h>
#include <jansson.h>

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
json_t *result_to_json(struct result *res);
void result_print(struct result *res, FILE *f);
void result_free(struct result *res);
