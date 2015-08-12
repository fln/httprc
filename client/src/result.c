#include "result.h"
#include "log.h"

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

json_t *result_to_json(struct result *res) {
	json_t *json;
	json_error_t error;

	json = json_pack_ex(&error, 0, "{s:s, s:i, s:s#, s:s#, s:i}",
		"id", res->id,
		"returnCode", res->return_code,
		"stdout", res->stdout_buffer, res->stdout_size,
		"stderr", res->stderr_buffer, res->stderr_size,
		"durationMs", res->duration_ms);
	if (!json) {
		log_error("json_pack_ex(): %s\n", error.text);
	}
	return json;
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
