#include "response_buf.h"

#include <stdlib.h>
#include <string.h>

size_t response_buf_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
        struct response_buf *buf;
        size_t data_size;

        buf = (struct response_buf *)userdata;
        data_size = size * nmemb;

        buf->start = realloc(buf->start, buf->size + data_size);
        if (!buf->start) {
                buf->size = 0;
                return 0;
        }
        memcpy(buf->start + buf->size, ptr, data_size);
        buf->size += data_size;

        return data_size;
}

void response_buf_reset(struct response_buf *buf) {
	buf->size = 0;
}

struct response_buf *response_buf_new() {
	struct response_buf *buf;

	buf = calloc(1, sizeof(*buf));
	return buf;
}

void response_buf_free(struct response_buf *buf) {
	if (buf == NULL) {
		return;
	}
	free(buf->start);
	free(buf);
}
