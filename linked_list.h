#ifndef LINKED_LIST_H
#define LINKED_LIST_H

typedef struct Node {
  int indexToConsume;
  struct Node *next;
} Node;

extern Node *head;

void pushNode(int indexToConsume);
int popNode();

#endif /* LINKED_LIST_H */
