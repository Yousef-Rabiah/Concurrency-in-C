#include <stdlib.h>
#include "linked_list.h"

// Creates and returns a new list
list_t* list_create()
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    list_t* list = (list_t *)malloc(sizeof(list_t));
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    return list;
}

// Destroys a list
void list_destroy(list_t* list)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    list_node_t* curr = list->head;
    while (curr != NULL){
      list_node_t* next = curr->next;
      free(curr);
      curr = next;
    }
    free(list);
}

// Returns head of the list
list_node_t* list_head(list_t* list)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    return list->head;
}

// Returns tail of the list
list_node_t* list_tail(list_t* list)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    return list->tail;
}

// Returns next element in the list
list_node_t* list_next(list_node_t* node)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    if (node!= NULL)
      return node->next;
    else
      return NULL;
}

// Returns prev element in the list
list_node_t* list_prev(list_node_t* node)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    if (node!= NULL)
      return node->prev;
    else
      return NULL;
}

// Returns end of the list marker
list_node_t* list_end(list_t* list)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    return list->tail;
}

// Returns data in the given list node
void* list_data(list_node_t* node)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    if (node!= NULL)
      return node->data;
    else
      return NULL;
}

// Returns the number of elements in the list
size_t list_count(list_t* list)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    return list->count;
}

// Finds the first node in the list with the given data
// Returns NULL if data could not be found
list_node_t* list_find(list_t* list, void* data)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    list_node_t* curr = list->head;
    while (curr != NULL){
      if (curr->data == data)
        return curr;
      curr = curr->next;
    }
    return NULL;
}

// Inserts a new node in the list with the given data
// Returns new node inserted
list_node_t* list_insert(list_t* list, void* data)
{
    // insert at the tail
    if (!list) {
        return NULL; // Handle null list pointer
    }
    list_node_t* new_node = (list_node_t*)malloc(sizeof(list_node_t));
    new_node->next = NULL;
    new_node->data = data;

    // Insert at the tail
    if (!list->head) {
        // If the list is empty, set head and tail to the new node
        list->head = new_node;
        list->tail = new_node;
        new_node->prev = NULL;
        list->count = list->count + 1;
    } else {
        // Otherwise, insert the new node at the tail
        list->tail->next = new_node;
        new_node->prev = list->tail;
        list->tail = new_node;
        list->count = list->count + 1;
    }
    return new_node;
}

// Removes a node from the list and frees the node resources
void list_remove(list_t* list, list_node_t* node)
{
    if (!list || !node) {
        return; // Handle null list or node
    }

    // If the node is the head of the list
    if (node == list->head) {
        list->head = node->next;
        if (list->head) {
            list->head->prev = NULL;
        } else {
            // The list is now empty
            list->tail = NULL;
        }
        list->count = list->count - 1;
    }
    // If the node is the tail of the list
    else if (node == list->tail) {
        list->tail = node->prev;
        if (list->tail) {
            list->tail->next = NULL;
        } else {
            // The list is now empty
            list->head = NULL;
        }
        list->count = list->count - 1;
    }
    // If the node is in the middle of the list
    else {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        list->count = list->count - 1;
    }

    // Free the node resources
    free(node);
}
