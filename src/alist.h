#pragma once

/**
 * @file alist.h
 * @author Scott Newman

 * @brief An array list data structure.
 *
 * This container represents a dynamically growing array. Upon initialization
 * of the array list, the capacity is set to 0 and no memory for the array list
 * is allocated. Once the first item is added, space for
 * #ALIST_CAPACITY_INITIAL items are allocated. If more room is needed after
 * that, the capacity is doubled.
 */

#include <stdbool.h>

#define ALIST_CAPACITY_INITIAL 256 //!< The default capacity of the list.

typedef struct alist_t alist_t;

/**
 * @brief Initializes the array list.
 *
 * This function must be called before any other array list function is used.
 * This will initialize the size and capacity to 0, so the first addition to
 * the array list will allocate room for #ALIST_CAPACITY_INITIAL items.
 *
 * @return A pointer to the list, or <tt>NULL</tt> if not enough memory was
 * available.
 */
alist_t * alist_init();

/**
 * @brief Frees the array list.
 *
 * This function is called once you're done with the array list and frees
 * any memory used by the array list. This does not free the user data that
 * was added to the array list. See alist_free_func() for that.
 *
 * @param[in] list The array list.
 */
void alist_free(alist_t *list);

/**
 * @brief Frees the array list and its user data.
 *
 * This function is called once you're done with the array list and frees
 * any memory used by the array list. For each item in the array list,
 * free_func() will be called on it to free the user data.
 *
 * @param[in] list The array list.
 * @param[in] free_func The function to call on each user data item left in the
 * array list.
 */
void alist_free_func(alist_t *list, void (*free_func)(void *));

/**
 * @brief Returns the size of the array list.
 *
 * Returns the number of items currently in the array list.
 *
 * @return The size of the array list.
 */
unsigned int alist_size(alist_t *list);

/**
 * @brief Adds an item to the array list.
 *
 * Adds an item onto the end of the array list, increasing the size of the
 * array list by one.
 *
 * @param[in] list The array list.
 * @param[in] data The user data to add.
 * @return <tt>true</tt>, otherwise <tt>false</tt> if not enough memory was
 * available.
 */
bool alist_add(alist_t *list, void *data);

/**
 * @brief Gets an item from the array list.
 *
 * Returns the user data located at the specified index.
 *
 * @param[in] list The array list.
 * @param[in] index The index of the array list to retrieve the user data from.
 * @return The user data, or <tt>NULL</tt> if the index is bigger than the size
 * of the array list.
 */
void * alist_get(alist_t *list, unsigned int index);

/**
 * @brief Removes an item from the array list.
 *
 * Removes an item from the array list at the specified index, decreases the
 * size of the array list by one, and also shifts all items after the index
 * down one. This function does not free the user data, which is why it's
 * returned. See alist_remove_func() if you want the array list to also free
 * the user data.
 *
 * @param[in] list The array list.
 * @param[in] index The index of the array list to remove the user data at.
 * @return The user data at the specified index, or <tt>NULL</tt> if the index
 * is bigger than the size of the array list.
 */
void * alist_remove(alist_t *list, unsigned int index);
