#pragma once

#include <stdbool.h>

#include <jansson.h>

#include "result.h"

struct command {
	const char   *id;
	const char   *command;
	const char  **args;
	size_t        arg_num;
	const char   *directory;
	const char  **environ_keys;
	const char  **environ_vals;
	size_t        environ_num;
	int           clean_environ;
	const char   *stdin_buffer;
	size_t        stdin_size;
	// constraints
	int     wait_time_ms;
	size_t  output_size;
};

struct command *command_parse(json_t *obj);
struct result *command_execute(struct command *c);
void command_print(struct command *c, FILE *f);
void command_free(struct command *c);
