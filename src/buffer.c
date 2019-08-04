#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "buffer.h"

struct buffer_t {
    unsigned char *data;
    size_t capacity;
    size_t len;
};

buffer_t *
buffer_init() {
    return buffer_init_ex(0);
}

buffer_t *
buffer_init_ex(size_t capacity) {
    buffer_t *buffer;

    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        return NULL;
    }

    if (capacity > 0) {
        buffer->data = malloc(capacity);
        if (buffer->data == NULL) {
            free(buffer);
            return false;
        }

        buffer->capacity = capacity;
    }

    return buffer;

}

void
buffer_free(buffer_t *buffer) {
    if (buffer == NULL) {
        return;
    }

    if (buffer->data != NULL) {
        free(buffer->data);
    }

    free(buffer);
}

size_t
buffer_length(buffer_t *buffer) {
    return buffer->len;
}

const unsigned char *
buffer_data(buffer_t *buffer) {
    return buffer->data;
}

static bool
buffer_grow(buffer_t *buffer, size_t len) {
    unsigned char *new_data;
    size_t new_capacity;

    if (buffer->capacity == 0) {
        new_capacity = len * 4;
    }
    else {
        new_capacity = (buffer->capacity * 2) + (len * 2);
    }

    new_data = realloc(buffer->data, new_capacity);
    if (new_data == NULL) {
        return false;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;

    return true;
}

bool
buffer_write(buffer_t *buffer, unsigned char *data, size_t len) {
    if (buffer->len + len > buffer->capacity) {
        if (!buffer_grow(buffer, len)) {
            return false;
        }
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;

    return true;
}

bool
buffer_writef(buffer_t *buffer, const char *fmt, ...) {
    char *buf;
    va_list ap;
    int len;
    bool success;

    va_start(ap, fmt);
    len = vasprintf(&buf, fmt, ap);
    va_end(ap);

    if (len == -1) {
        return false;
    }

    success = buffer_write(buffer, (unsigned char *)buf, len);
    free(buf);

    return success;
}
