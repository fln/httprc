#pragma once

#define err_fatal(x) \
	do { \
		if ((x) == -1) \
			fatal_error(PACKAGE_NAME ":%s:%d: %s = -1\n", __FILE__, __LINE__, #x);\
	} while(0)

void fatal_error(const char *format, ...);
