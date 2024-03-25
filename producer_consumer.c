#include "producer_consumer.h"
#include "linked_list.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Producer thread function for loading files into input buffer.
 *
 * This function loads files into buffer index based on the filename.
 * It also signals consumers when new data is available.
 *
 * @param args Pointer to the thread arguments struct containing buffer
 * information.
 * @return NULL after completing the producer thread's tasks.
 */
void *producerThread(void *args) {
  ThreadArgs *threadArgs = (ThreadArgs *)args;
  // determine work size for each producer; needed for various file numbers
  pthread_mutex_lock(&producer_mutex);
  int workSize = isFirstProducer ? producerRemainderWork : producerRegularWork;
  isFirstProducer = 0;
  pthread_mutex_unlock(&producer_mutex);

  for (int i = 0; i < workSize; i++) {
    pthread_mutex_lock(&mutex);
    while (fileIndex == numFiles) { // buffer is full
      pthread_cond_wait(&cond_producer, &mutex);
    }
    int localFileIndex = fileIndex;
    fileIndex++;
    pthread_mutex_unlock(&mutex);

    // load file
    char tempBuffer[fullPathLength];
    strcpy(tempBuffer, fullPath);
    strcat(tempBuffer, fileNames[localFileIndex]);
    FILE *inputFile = fopen(tempBuffer, "r");
    assert(inputFile != NULL);

    // add to buffer index based on filename
    int fileOrder = convertFilenameToOrder(fileNames[localFileIndex]) - 1;
    pthread_mutex_lock(&threadArgs->inputBufferMutexes[fileOrder]);
    threadArgs->inputBuffer[fileOrder].dataSize =
        fread(threadArgs->inputBuffer[fileOrder].data, sizeof(unsigned char),
              bufferSize, inputFile);
    fclose(inputFile);
    pthread_mutex_unlock(&threadArgs->inputBufferMutexes[fileOrder]);

    // add the buffer index to the linked list, used by consumers
    pthread_mutex_lock(&linked_list_mutex);
    pushNode(fileOrder);
    pthread_mutex_unlock(&linked_list_mutex);
    pthread_cond_signal(&cond_consumer);
  }
  return NULL;
}

/**
 * @brief Consumer thread function for processing data from input buffers.
 *
 * This function loads data from input buffer and performs compression using
 * zlib, and stores the compressed data in output buffers. It signals producers
 * once work is done.
 *
 * @param args Pointer to the thread arguments struct containing buffer
 * information.
 * @return NULL after completing the consumer thread's tasks.
 */
void *consumerThread(void *args) {
  ThreadArgs *threadArgs = (ThreadArgs *)args;
  // determine work size for each consumer; needed for various file numbers
  pthread_mutex_lock(&consumer_mutex);
  int workSize = isFirstConsumer ? consumerRemainderWork : consumerRegularWork;
  isFirstConsumer = 0;
  pthread_mutex_unlock(&consumer_mutex);

  for (int i = 0; i < workSize; i++) {
    pthread_mutex_lock(&linked_list_mutex);
    while (head == NULL) { // buffer is empty
      pthread_cond_wait(&cond_consumer, &linked_list_mutex);
    }
    int indexToConsume = popNode();
    pthread_mutex_unlock(&linked_list_mutex);

    // zip file
    z_stream strm;
    int ret = deflateInit(&strm, 9);
    assert(ret == Z_OK);
    strm.avail_in = threadArgs->inputBuffer[indexToConsume].dataSize;
    strm.next_in = threadArgs->inputBuffer[indexToConsume].data;
    strm.avail_out = bufferSize;
    strm.next_out = threadArgs->outputBuffer[indexToConsume].data;
    ret = deflate(&strm, Z_FINISH);
    assert(ret == Z_STREAM_END);

    // store compressed data in output buffer index based on index from linked
    // list
    pthread_mutex_lock(&threadArgs->outputBufferMutexes[indexToConsume]);
    threadArgs->outputBuffer[indexToConsume].dataSize =
        bufferSize - strm.avail_out;
    pthread_mutex_unlock(&threadArgs->outputBufferMutexes[indexToConsume]);
    pthread_cond_signal(&cond_producer);
  }
  return NULL;
}
