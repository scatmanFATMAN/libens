#pragma once

/**
 * @file queue.h
 * @author Scott Newman
 *
 * @brief A queue data structure.
 *
 * A generic queue data structure that uses previous and next pointers in
 * a linked list. All data put on the queue is appended to the back and all
 * data removed from the queue is removed from the front. This means the queue
 * is FIFO (first in, first out).
 */

#include <stdbool.h>

typedef struct queue_t queue_t;

/**
 * @brief Initializes the queue.
 *
 * Initializes the queue and sets the sentinel nodes to NULL. This function
 * must be the first function called before using any other queue functions.
 *
 * @param[in] queue The queue.
 */
queue_t * queue_init();

/**
 * @brief Frees the memory used by the queue.
 *
 * Releases the memory used by the queue. This should be the last function
 * called on the queue. This function does not free the user data associated
 * with each node in the queue, if there are any left, and must be done by the
 * caller. See queue_free_func() for a convience function to also free the
 * user data.
 *
 * @param[in] queue The queue.
 */
void queue_free(queue_t *queue);

/**
 * @brief Frees the memory used by the queue, also freeing the user data.
 *
 * Releases the memory used by the queue. This should be the last function
 * called on the queue. This function will free the user data of each node, if
 * any exist, by calling <tt>free_func</tt> on each pointer to user data.
 *
 * @param[in] queue The queue.
 * @param[in] free_func The function to call on each node's user data to free
 * its memory.
 */
void queue_free_func(queue_t *queue, void (*free_func)(void *));

/**
 * @brief Gets the queue's size.
 *
 * Returns the number of nodes in the queue.
 *
 * @param[in] queue The queue.
 * @return The queue's size.
 */
unsigned int queue_size(queue_t *queue);

/**
 * @brief Pushes data onto the back of the queue.
 *
 * Adds a new node to the end of the queue. The queue's tail pointer will now
 * point to this new node.
 *
 * @param[in] queue The queue.
 * @param[in] data A pointer to the data to add.
 * @return <tt>true</tt>, otherwise false is not enough memory was available.
 */
bool queue_push(queue_t *queue, void *data);

/**
 * @brief Pops data off front of the queue.
 *
 * Removes a node from the front of the queue and returns the user data. This
 * function may be safely called if the queue size is empty, in which case
 * <tt>NULL</tt> is returned.
 *
 * @param[in] queue The queue.
 * @return The user data of the first node, or NULL if the queue is empty.
 */
void * queue_pop(queue_t *queue);

/**
 * @brief Peeks the front of the queue.
 *
 * Returns the user data from the front of the queue but does not remove it.
 * This function may be safely called if the queue is empty, in which case
 * <tt>NULL</tt> is returned.
 *
 * @param[in] queue The queue.
 * @return The user data of the first node, or NULL if the queue is empty.
 */
void * queue_peek(queue_t *queue);
