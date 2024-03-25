#include "linked_list.h"
#include <assert.h>
#include <stdlib.h>

Node *head = NULL;

/**
 * @brief Prepend a new node with the specified index into the linked list.
 * @param indexToConsume The index value to be stored in the new node.
 */
void pushNode(int indexToConsume) {
  Node *newNode = malloc(sizeof(Node));
  assert(newNode != NULL);
  newNode->indexToConsume = indexToConsume;
  newNode->next = head;
  head = newNode;
}

/**
 * @brief Pop the node at the beginning of the linked list and return its index
 * value.
 * @return The index value of the popped node, or -1 if the list is empty.
 */
int popNode() {
  if (head == NULL) {
    return -1;
  }
  int indexToConsume = head->indexToConsume;
  Node *temp = head;
  head = head->next;
  free(temp);
  return indexToConsume;
}
