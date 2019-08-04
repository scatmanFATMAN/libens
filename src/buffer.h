#pragma once

/**
 * @file buffer.c
 * @author scott.c
 *
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct buffer_t buffer_t;

/**
 * Allocates and initializes a buffer.
 *
 * @return A buffer, or <tt>NULL</tt> if not enough memory was available.
 */
buffer_t * buffer_init();

/**
 * Allocates and initializes a buffer with some extra flags.
 *
 * @param[in] capacity An initial capacity to allocate room for.
 * @return A buffer, or <tt>NULL</tt> if not enough memory was available.
 */
buffer_t * buffer_init_ex(size_t capacity);

/**
 * Deallocates a buffer which was allocated with buffer_init().
 *
 * @param[in] buffer The buffer.
 */
void buffer_free(buffer_t *buffer);

/**
 * Returns the length of the buffer.
 *
 * @param[in] buffer The buffer.
 * @return The size of the buffer.
 */
size_t buffer_length(buffer_t *buffer);

/**
 * Returns a pointer to the buffer's data.
 *
 * @param[in] buffer The buffer.
 * @return A pointer to the buffer's data.
 */
const unsigned char * buffer_data(buffer_t *buffer);

/**
 * Writes <tt>len</tt> bytes of data from the pointer pointing to
 * <tt>data</tt> to the buffer.
 *
 * @param[in] buffer The buffer.
 * @param[in] data A pointer to data to write.
 * @param[in] len The number of bytes to write.
 * @return true if the write was successful, otherwise false if not enough
 * memory was available.
 */
bool buffer_write(buffer_t *buffer, unsigned char *data, size_t len);

/**
 * Writes a formatted string to the buffer.
 *
 * @param[in] buffer The buffer.
 * @param[in] fmt The formatted printf-style string.
 * @param[in] ... Variables for <tt>fmt</tt>.
 * @return true if the write was successful, otherwise false if not enough
 * memory was available.
 */
bool buffer_writef(buffer_t *buffer, const char *fmt, ...);
