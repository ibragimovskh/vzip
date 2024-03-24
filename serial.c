#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#define NUM_PRODUCER_THREADS 8
#define NUM_CONSUMER_THREADS 11

// Mutex and condition variable declarations
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t linked_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t producer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t consumer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_producer = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_consumer = PTHREAD_COND_INITIALIZER;

typedef struct {
    unsigned char* data;
    int dataSize;
} Buffer;

// Thread argument struct
typedef struct {
    Buffer* inputBuffer;
    Buffer* outputBuffer;
    pthread_mutex_t* inputBufferMutexes;
    pthread_mutex_t* outputBufferMutexes;
} ThreadArgs;

typedef struct Node {
    int indexToConsume;
    struct Node* next;
} Node;

char** ppmSource;
char** fileNames = NULL;
char* fullPath = NULL;
int fullPathLength = 0;
Node* head = NULL;
int fileIndex = 0;
int totalInputSize = 0, totalOutputSize = 0;
int bufferSize = 0;
int numFiles = 0;
int producerRegularWork = 0, producerRemainderWork = 0;
int consumerRegularWork = 0, consumerRemainderWork = 0;
int isFirstProducer = 1, isFirstConsumer = 1;

int convertFilenameToOrder(const char* filename) {
    int order;
    sscanf(filename, "%d", &order);
    return order;
}

void pushNode(int indexToConsume) {
    Node* newNode = malloc(sizeof(Node));
    assert(newNode != NULL);
    newNode->indexToConsume = indexToConsume;
    newNode->next = head;
    head = newNode;
}

int popNode() {
    if (head == NULL) {
        return -1;
    }

    int indexToConsume = head->indexToConsume;
    Node* temp = head;
    head = head->next;
    free(temp);
    return indexToConsume;
}

void* producerThread(void* args) {
    ThreadArgs* threadArgs = (ThreadArgs*)args;
    pthread_mutex_lock(&producer_mutex);
    int workSize = isFirstProducer ? producerRemainderWork : producerRegularWork;
    isFirstProducer = 0;
    pthread_mutex_unlock(&producer_mutex);

    for (int i = 0; i < workSize; i++) {
        pthread_mutex_lock(&mutex);
        while (fileIndex == numFiles) { // buffer is full
            pthread_cond_wait(&cond_producer, &mutex);
        }
        // create local copies of fileIndex and count
        int localFileIndex = fileIndex;
        fileIndex++;
        pthread_mutex_unlock(&mutex);

        // produce: load file
        char tempBuffer[fullPathLength];
        strcpy(tempBuffer, fullPath);
        strcat(tempBuffer, fileNames[localFileIndex]);
        FILE* inputFile = fopen(tempBuffer, "r");
        assert(inputFile != NULL);

        // add to buffer
        int fileOrder = convertFilenameToOrder(fileNames[localFileIndex]) - 1;
        pthread_mutex_lock(&threadArgs->inputBufferMutexes[fileOrder]);
        threadArgs->inputBuffer[fileOrder].dataSize =
            fread(threadArgs->inputBuffer[fileOrder].data, sizeof(unsigned char),
                  bufferSize, inputFile);
        fclose(inputFile);
        pthread_mutex_unlock(&threadArgs->inputBufferMutexes[fileOrder]);
        pthread_mutex_lock(&linked_list_mutex);
        pushNode(fileOrder);
        pthread_mutex_unlock(&linked_list_mutex);
        pthread_cond_signal(&cond_consumer);
    }
    return NULL;
}

void* consumerThread(void* args) {
    ThreadArgs* threadArgs = (ThreadArgs*)args;
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

        pthread_mutex_lock(&threadArgs->outputBufferMutexes[indexToConsume]);
        threadArgs->outputBuffer[indexToConsume].dataSize = bufferSize - strm.avail_out;
        pthread_mutex_unlock(&threadArgs->outputBufferMutexes[indexToConsume]);
        pthread_cond_signal(&cond_producer);
    }
    return NULL;
}

void initializeThreadArgs(ThreadArgs* threadArgs, int numFiles, int bufferSize) {
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

void cleanupResources(ThreadArgs* threadArgs, int numFiles) {
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

int getFileSize() {
    int len = strlen(ppmSource[1]) + strlen(fileNames[0]) + 2;
    char* fullPath = malloc(len * sizeof(char));
    assert(fullPath != NULL);
    strcpy(fullPath, ppmSource[1]);
    strcat(fullPath, "/");
    strcat(fullPath, fileNames[0]);
    FILE* file = fopen(fullPath, "r");
    assert(file != NULL);
    fseek(file, 0, SEEK_END);
    int size = ftell(file);
    fclose(file);
    return size;
}

int main(int argc, char** argv) {
    // time computation header
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    // end of time computation header

    // do not modify the main function before this point!

    assert(argc == 2);
    ppmSource = argv;

    DIR* dir;
    struct dirent* dirEntry;

    dir = opendir(argv[1]);
    if (dir == NULL) {
        printf("An error has occurred\n");
        return 0;
    }

    // create list of PPM files
    while ((dirEntry = readdir(dir)) != NULL) {
        fileNames = realloc(fileNames, (numFiles + 1) * sizeof(char*));
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
    FILE* outputFile = fopen("video.vzip", "w");
    assert(outputFile != NULL);

    bufferSize = getFileSize();
    totalInputSize = numFiles * bufferSize;
    producerRegularWork = (int)ceil((double)numFiles / NUM_PRODUCER_THREADS);
    producerRemainderWork = numFiles - (producerRegularWork * (NUM_PRODUCER_THREADS - 1));
    consumerRegularWork = (int)ceil((double)numFiles / NUM_CONSUMER_THREADS);
    consumerRemainderWork = numFiles - (consumerRegularWork * (NUM_CONSUMER_THREADS - 1));

    fullPathLength = strlen(ppmSource[1]) + strlen(fileNames[0]) + 2;
    fullPath = (char*)malloc(fullPathLength * sizeof(char));
    assert(fullPath != NULL);
    strcpy(fullPath, ppmSource[1]);
    strcat(fullPath, "/");

    // create producer and consumer threads
    pthread_t producerThreads[NUM_PRODUCER_THREADS];
    pthread_t consumerThreads[NUM_CONSUMER_THREADS];

    ThreadArgs threadArgs;
    initializeThreadArgs(&threadArgs, numFiles, bufferSize);

    for (int i = 0; i < NUM_PRODUCER_THREADS; i++) {
        if (pthread_create(&producerThreads[i], NULL, producerThread, (void*)&threadArgs) != 0) {
            perror("Failed to create producer thread");
        }
    }
    for (int i = 0; i < NUM_CONSUMER_THREADS; i++) {
        if (pthread_create(&consumerThreads[i], NULL, consumerThread, (void*)&threadArgs) != 0) {
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

    for (int i = 0; i < numFiles; i++) {
        fwrite(&threadArgs.outputBuffer[i].dataSize, sizeof(int), 1, outputFile);
        fwrite(threadArgs.outputBuffer[i].data, sizeof(unsigned char),
               threadArgs.outputBuffer[i].dataSize, outputFile);
        totalOutputSize += threadArgs.outputBuffer[i].dataSize;
    }
    fclose(outputFile);
    printf("Compression rate: %.2lf%%\n", 100.0 * (totalInputSize - totalOutputSize) / totalInputSize);

    // release list of files
    cleanupResources(&threadArgs, numFiles);

    // do not modify the main function after this point!

    // time computation footer
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Time: %.2f seconds\n",
           ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) -
               ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));
    // end of time computation footer

    return 0;
}