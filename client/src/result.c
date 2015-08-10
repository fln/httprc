#include "result.h"

#include <stdio.h>
#include <stdlib.h>

struct result *result_new(size_t buf_size) {
	struct result *res;

	res = calloc(1, sizeof(*res));
	if (res) {
		res->stdout_buffer = malloc(buf_size);
		res->stderr_buffer = malloc(buf_size);
	}
	return res;
}

void result_print(struct result *res) {
	printf("--- Result ---\n");
	printf("\tId:          %s\n", res->id);
	printf("\tReturn code: %d\n", res->return_code);
	printf("\tDuration:    %d ms\n", res->duration_ms);
	printf("\tStdout[%zu]: %.*s\n", res->stdout_size, (int)res->stdout_size, res->stdout_buffer);
	printf("\tStderr[%zu]: %.*s\n", res->stderr_size, (int)res->stderr_size, res->stderr_buffer);
}

void result_free(struct result *res) {
	if (res) {
		free(res->stdout_buffer);
		free(res->stderr_buffer);
	}
	free(res);
}
