/**
 * @file alist.c
 */

#include <stdlib.h>
#include <string.h>
#include "alist.h"

/**
 * @brief An array list item.
 *
 * This structure represnts the data in the array list.
 */
typedef struct {
    void *data; //!< A pointer to the data in the list.
} alist_item_t;

/**
 * @brief The array list.
 *
 * This structure represents the array list.
 */
struct alist_t {
    alist_item_t *items;    //!< The array of items.   
    unsigned int size;      //!< The size of the array list.
    unsigned int capacity;  //!< The capacity of the array list.
};

alist_t *
alist_init() {
    alist_t *list;

    list = calloc(1, sizeof(*list));

    return list;
}

void
alist_free(alist_t *list) {
    alist_free_func(list, NULL);
}

void
alist_free_func(alist_t *list, void (*free_func)(void *)) {
    unsigned int i;

    if (list == NULL) {
        return;
    }

    if (list->items != NULL) {
        for (i = 0; i < list->size; i++) {
            if (free_func != NULL) {
                free_func(list->items[i].data);
            }
        }

        free(list->items);
    }

    free(list);
}

unsigned int
alist_size(alist_t *list) {
    return list->size;
}

static bool
alist_grow(alist_t *list) {
    alist_item_t *new_items;
    unsigned int new_capacity;

    new_capacity = list->capacity == 0 ? ALIST_CAPACITY_INITIAL : list->capacity * 2;
    new_items = realloc(list->items, sizeof(alist_item_t) * new_capacity);
    if (new_items == NULL) {
        return false;
    }

    list->items = new_items;
    list->capacity = new_capacity;

    return true;
}

bool
alist_add(alist_t *list, void *data) {
    if (list->size >= list->capacity) {
        if (!alist_grow(list)) {
            return false;
        }
    }

    list->items[list->size++].data = data;
    return true;
}

void *
alist_get(alist_t *list, unsigned int index) {
    return index < list->size ? list->items[index].data : NULL;
}

void *
alist_remove(alist_t *list, unsigned int index) {
    void *data;

    if (index >= list->size) {
        return NULL;
    }

    data = list->items[index].data;
    --list->size;

    if (index < list->size && list->size - index > 0) {
        memmove(list->items + index, list->items + index + 1, sizeof(alist_item_t) * (list->size - index));
    }

    return data;
}
