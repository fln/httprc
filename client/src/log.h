#pragma once

#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#define log_std_error(x) \
	do { \
		if ((x) == -1) \
			fprintf(stderr, PACKAGE_NAME ":%s:%d: %s = -1, %s (%d)\n", __FILE__, __LINE__, #x, strerror(errno), errno);\
	} while(0)

#define die_std_error(x) \
	do { \
		if ((x) == -1) { \
			fprintf(stderr, PACKAGE_NAME ":%s:%d: %s = -1, %s (%d)\n", __FILE__, __LINE__, #x, strerror(errno), errno); \
			exit(EXIT_FAILURE); \
		} \
	} while(0)

#define log_error(format, ...) fprintf(stderr, PACKAGE_NAME ": " format, __VA_ARGS__)
