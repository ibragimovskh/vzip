#include "helpers.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t linked_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t producer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t consumer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_producer = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_consumer = PTHREAD_COND_INITIALIZER;

char **ppmSource;
char **fileNames = NULL;
char *fullPath = NULL;
int fullPathLength = 0;
int fileIndex = 0;
int totalInputSize = 0, totalOutputSize = 0;
int bufferSize = 0;
int numFiles = 0;
int producerRegularWork = 0, producerRemainderWork = 0;
int consumerRegularWork = 0, consumerRemainderWork = 0;
int isFirstProducer = 1, isFirstConsumer = 1;

/**
 * @brief Extract the order from the filename.
 * @param filename The filename containing the order information.
 * @return The integer order extracted from the filename.
 */
int convertFilenameToOrder(const char *filename) {
  int order;
  sscanf(filename, "%d", &order);
  return order;
}

/**
 * @brief Cleanup and free resources allocated by the thread arguments and
 * fileNames array.
 * @param threadArgs Pointer to the thread arguments struct.
 * @param numFiles The number of files for which resources were allocated.
 */
void cleanupResources(ThreadArgs *threadArgs, int numFiles) {
  for (int i = 0; i < numFiles; i++) {
    free(threadArgs->inputBuffer[i].data);
    free(threadArgs->outputBuffer[i].data);
  }
  free(threadArgs->inputBuffer);
  free(threadArgs->outputBuffer);
  free(threadArgs->inputBufferMutexes);
  free(threadArgs->outputBufferMutexes);
  free(fullPath);
  for (int i = 0; i < numFiles; i++)
    free(fileNames[i]);
  free(fileNames);
}

/**
 * @brief Get the size of a file specified by the first file name in the
 * fileNames array.
 * @return The size of the file in bytes, or -1 if there was an error opening or
 * determining the size of the file.
 */
int getFileSize() {
  int len = strlen(ppmSource[1]) + strlen(fileNames[0]) + 2;
  char *fullPath = malloc(len * sizeof(char));
  assert(fullPath != NULL);
  strcpy(fullPath, ppmSource[1]);
  strcat(fullPath, "/");
  strcat(fullPath, fileNames[0]);
  FILE *file = fopen(fullPath, "r");
  assert(file != NULL);
  fseek(file, 0, SEEK_END);
  int size = ftell(file);
  fclose(file);
  return size;
}

/**
 * @brief Initialize thread arguments with buffers and mutexes.
 * @param threadArgs Pointer to the thread arguments struct to be initialized.
 * @param numFiles The number of files for which buffers and mutexes are needed.
 * @param bufferSize The size of the buffer for each file.
 */
void initializeThreadArgs(ThreadArgs *threadArgs, int numFiles,
                          int bufferSize) {
  threadArgs->inputBuffer = malloc(numFiles * sizeof(Buffer));
  if (threadArgs->inputBuffer == NULL) {
    perror("Failed to allocate memory");
    exit(1);
  }
  threadArgs->outputBuffer = malloc(numFiles * sizeof(Buffer));
  if (threadArgs->outputBuffer == NULL) {
    perror("Failed to allocate memory");
    exit(1);
  }
  threadArgs->inputBufferMutexes = malloc(numFiles * sizeof(pthread_mutex_t));
  if (threadArgs->inputBufferMutexes == NULL) {
    perror("Failed to allocate memory");
    exit(1);
  }
  threadArgs->outputBufferMutexes = malloc(numFiles * sizeof(pthread_mutex_t));
  if (threadArgs->outputBufferMutexes == NULL) {
    perror("Failed to allocate memory");
    exit(1);
  }

  for (int i = 0; i < numFiles; i++) {
    threadArgs->inputBuffer[i].data = malloc(bufferSize);
    threadArgs->outputBuffer[i].data = malloc(bufferSize);
    threadArgs->inputBuffer[i].dataSize = 0;
    threadArgs->outputBuffer[i].dataSize = 0;
    pthread_mutex_init(&threadArgs->inputBufferMutexes[i], NULL);
    pthread_mutex_init(&threadArgs->outputBufferMutexes[i], NULL);
  }
  assert(threadArgs->inputBufferMutexes != NULL &&
         threadArgs->outputBufferMutexes != NULL &&
         threadArgs->inputBuffer != NULL && threadArgs->outputBuffer != NULL);
}
