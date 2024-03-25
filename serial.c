#include "helpers.h"
#include "producer_consumer.h"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

int main(int argc, char **argv) {
  // time computation header
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  // end of time computation header
  // do not modify the main function before this point!

  assert(argc == 2);
  ppmSource = argv;

  DIR *dir;
  struct dirent *dirEntry;

  dir = opendir(argv[1]);
  if (dir == NULL) {
    printf("An error has occurred\n");
    return 0;
  }

  // create list of PPM files
  while ((dirEntry = readdir(dir)) != NULL) {
    fileNames = realloc(fileNames, (numFiles + 1) * sizeof(char *));
    assert(fileNames != NULL);

    int len = strlen(dirEntry->d_name);
    if (dirEntry->d_name[len - 4] == '.' && dirEntry->d_name[len - 3] == 'p' &&
        dirEntry->d_name[len - 2] == 'p' && dirEntry->d_name[len - 1] == 'm') {
      fileNames[numFiles] = strdup(dirEntry->d_name);
      assert(fileNames[numFiles] != NULL);

      numFiles++;
    }
  }
  closedir(dir);

  FILE *outputFile = fopen("video.vzip", "w");
  assert(outputFile != NULL);
  fullPathLength = strlen(ppmSource[1]) + strlen(fileNames[0]) + 2;
  fullPath = (char *)malloc(fullPathLength * sizeof(char));
  assert(fullPath != NULL);
  strcpy(fullPath, ppmSource[1]);
  strcat(fullPath, "/");

  // determine work size for each producer and consumer
  bufferSize = getFileSize();
  totalInputSize = numFiles * bufferSize;
  producerRegularWork = (int)ceil((double)numFiles / NUM_PRODUCER_THREADS);
  producerRemainderWork =
      numFiles - (producerRegularWork * (NUM_PRODUCER_THREADS - 1));
  consumerRegularWork = (int)ceil((double)numFiles / NUM_CONSUMER_THREADS);
  consumerRemainderWork =
      numFiles - (consumerRegularWork * (NUM_CONSUMER_THREADS - 1));

  // create producer and consumer threads
  pthread_t producerThreads[NUM_PRODUCER_THREADS];
  pthread_t consumerThreads[NUM_CONSUMER_THREADS];
  ThreadArgs threadArgs;
  initializeThreadArgs(&threadArgs, numFiles, bufferSize);

  for (int i = 0; i < NUM_PRODUCER_THREADS; i++) {
    if (pthread_create(&producerThreads[i], NULL, producerThread,
                       (void *)&threadArgs) != 0) {
      perror("Failed to create producer thread");
    }
  }
  for (int i = 0; i < NUM_CONSUMER_THREADS; i++) {
    if (pthread_create(&consumerThreads[i], NULL, consumerThread,
                       (void *)&threadArgs) != 0) {
      perror("Failed to create consumer thread");
    }
  }

  for (int i = 0; i < NUM_PRODUCER_THREADS; i++) {
    if (pthread_join(producerThreads[i], NULL) != 0) {
      perror("Failed to join thread");
    }
  }
  for (int i = 0; i < NUM_CONSUMER_THREADS; i++) {
    if (pthread_join(consumerThreads[i], NULL) != 0) {
      perror("Failed to join thread");
    }
  }

  // output buffer is sorted by default (consumer inserts on filename order)
  for (int i = 0; i < numFiles; i++) {
    fwrite(&threadArgs.outputBuffer[i].dataSize, sizeof(int), 1, outputFile);
    fwrite(threadArgs.outputBuffer[i].data, sizeof(unsigned char),
           threadArgs.outputBuffer[i].dataSize, outputFile);
    totalOutputSize += threadArgs.outputBuffer[i].dataSize;
  }
  fclose(outputFile);
  cleanupResources(&threadArgs, numFiles);

  // do not modify the main function after this point!
  printf("Compression rate: %.2lf%%\n",
         100.0 * (totalInputSize - totalOutputSize) / totalInputSize);

  // time computation footer
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Time: %.2f seconds\n",
         ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) -
             ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));
  // end of time computation footer

  return 0;
}