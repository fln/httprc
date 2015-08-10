#pragma once

#include <stddef.h>
#include <stdint.h>

struct response_buf {
	char   *start;
	size_t  size;
};

struct response_buf *response_buf_new();
void response_buf_reset();
size_t response_buf_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata);
void response_buf_free(struct response_buf *buf);
