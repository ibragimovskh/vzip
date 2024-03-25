#ifndef HELPERS_H
#define HELPERS_H

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern pthread_mutex_t mutex;
extern pthread_mutex_t linked_list_mutex;
extern pthread_mutex_t producer_mutex;
extern pthread_mutex_t consumer_mutex;
extern pthread_cond_t cond_producer;
extern pthread_cond_t cond_consumer;

typedef struct {
  unsigned char *data;
  int dataSize;
} Buffer;

typedef struct {
  Buffer *inputBuffer;
  Buffer *outputBuffer;
  pthread_mutex_t *inputBufferMutexes;
  pthread_mutex_t *outputBufferMutexes;
} ThreadArgs;

extern char **ppmSource;
extern char **fileNames;
extern char *fullPath;
extern int fullPathLength;
extern int fileIndex;
extern int totalInputSize, totalOutputSize;
extern int bufferSize;
extern int numFiles;
extern int producerRegularWork, producerRemainderWork;
extern int consumerRegularWork, consumerRemainderWork;
extern int isFirstProducer, isFirstConsumer;

int convertFilenameToOrder(const char *filename);
void cleanupResources(ThreadArgs *threadArgs, int numFiles);
int getFileSize();
void initializeThreadArgs(ThreadArgs *threadArgs, int numFiles, int bufferSize);

#endif /* HELPERS_H */
