#include "result.h"

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

void result_free(struct result *res) {
	if (res) {
		free(res->stdout_buffer);
		free(res->stderr_buffer);
	}
	free(res);
}
