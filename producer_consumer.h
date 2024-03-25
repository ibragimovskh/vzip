#ifndef PRODUCER_CONSUMER_H
#define PRODUCER_CONSUMER_H

#include "helpers.h"
#include <pthread.h>
#include <zlib.h>

#define NUM_PRODUCER_THREADS 8
#define NUM_CONSUMER_THREADS 11

void *producerThread(void *args);
void *consumerThread(void *args);

#endif /* PRODUCER_CONSUMER_H */
