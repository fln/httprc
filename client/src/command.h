#pragma once

#include <stdbool.h>

#include <jansson.h>

#include "result.h"

struct command {
	const char   *id;
	bool          daemon_mode;
	const char   *command;
	const char  **args;
	size_t        arg_num;
	const char  **environ;
	const char   *stdin_buffer;
	size_t        stdin_size;
	// constraints
	int     wait_time_ms;
	size_t  output_size;
};

int command_parse(struct command *c, json_t *obj);
void command_print(struct command *c);
struct result *command_execute(struct command *c);
void command_free(struct command *c);
