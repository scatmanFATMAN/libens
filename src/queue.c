/**
 * @file queue.c
 */

#include <stdlib.h>
#include <string.h>
#include "queue.h"

/**
 * @brief The queue node structure.
 *
 * This structure represents the node that make up the queue. The
 * <tt>data</tt> pointer is a pointer to the user data. The <tt>prev</tt> node
 * pointer points to the previous node in the queue, and for the first node in
 * the queue, points to NULL. The <tt>next</tt> node pointer points to the
 * next node in the queue, and for the last node in queue, points to NULL.
 */
typedef struct queue_node_t {
    void *data;                 //!< The user data.
    struct queue_node_t *prev;  //!< The previous node in the queue.
    struct queue_node_t *next;  //!< The next node in the queue.
} queue_node_t;

/**
 * @brief The queue structure.
 *
 * This structure represents the queue itself with sentinel head and tail
 * nodes, providing quick access to either end of the queue.
 */
struct queue_t {
    queue_node_t *head; //!< Points to the first node in the queue.
    queue_node_t *tail; //!< Points to the last node in the queue.
    unsigned int size;  //!< The number of nodes in the queue.
};

queue_t *
queue_init() {
    queue_t *queue;

    queue = calloc(1, sizeof(*queue));

    return queue;
}

void
queue_free(queue_t *queue) {
    queue_free_func(queue, NULL);
}

void
queue_free_func(queue_t *queue, void (*free_func)(void *)) {
    queue_node_t *node, *del;

    if (queue == NULL) {
        return;
    }

    node = queue->head;
    while (node != NULL) {
        del = node;
        node = node->next;

        if (free_func != NULL) {
            free_func(del->data);
        }

        free(del);
    }

    free(queue);
}

unsigned int
queue_size(queue_t *queue) {
    return queue->size;
}

bool
queue_push(queue_t *queue, void *data) {
    queue_node_t *node;

    node = malloc(sizeof(*node));
    if (node == NULL) {
        return false;
    }

    node->data = data;
    node->next = NULL;

    if (queue->head == NULL) {
        node->prev = NULL;
        queue->head = node;
        queue->tail = node;
    }
    else {
        node->prev = queue->tail;
        queue->tail->next = node;
        queue->tail = node;
    }

    ++queue->size;

    return true;
}

void *
queue_pop(queue_t *queue) {
    queue_node_t *node;
    void *data;

    if (queue->head == NULL) {
        return NULL;
    }

    node = queue->head;
    data = queue->head->data;

    if (node->next == NULL) {
        queue->head = NULL;
        queue->tail = NULL;
    }
    else {
        queue->head = node->next;
        node->prev = NULL;
    }

    free(node);
    --queue->size;

    return data;
}

void *
queue_peek(queue_t *queue) {
    return queue->head == NULL ? NULL : queue->head->data;
}
