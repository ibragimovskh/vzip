#include <assert.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#define BUFFER_SIZE 1048576 // 1MB
#define PRODUCER_THREADS_NUM 10
#define CONSUMER_THREADS_NUM 10
#define FILE_COUNT 100

typedef struct node {
  int index_to_consume;
  struct node *next;
} node_t;
node_t *head = NULL;

typedef struct {
  unsigned char data[BUFFER_SIZE];
  int nbytes;
} buffer_t;
buffer_t buffer_in[FILE_COUNT];
buffer_t buffer_out[FILE_COUNT];

int count = 0, file_idx = 0;
int total_in = 0, total_out = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t linked_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t buffer_in_index_mutexes[FILE_COUNT];
pthread_mutex_t buffer_out_index_mutexes[FILE_COUNT];
pthread_cond_t cond_producer = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_consumer = PTHREAD_COND_INITIALIZER;

char **ppm_source;
char **files = NULL;

int convert_filename_to_order(const char *filename) {
  int order;
  sscanf(filename, "%d", &order);
  return order;
}

void push(int index_to_consume) {
  node_t *new_node = malloc(sizeof(node_t));
  assert(new_node != NULL);
  new_node->index_to_consume = index_to_consume;
  new_node->next = head;
  head = new_node;
}

int pop() {
  if (head == NULL) {
    return -1;
  }

  int index_to_consume = head->index_to_consume;
  node_t *temp = head;
  head = head->next;
  free(temp);
  return index_to_consume;
}

void print_list() {
  node_t *current = head;
  while (current != NULL) {
    printf("%d -> ", current->index_to_consume);
    current = current->next;
  }
  printf("NULL\n");
}

void *producer(void *args) {
  for (int i = 0; i < FILE_COUNT / PRODUCER_THREADS_NUM; i++) {
    pthread_mutex_lock(&mutex);
    while (file_idx == FILE_COUNT) { // buffer is full
      pthread_cond_wait(&cond_producer, &mutex);
    }
    // create local copies of file_idx and count
    int local_file_idx = file_idx;
    file_idx++;
    pthread_mutex_unlock(&mutex);

    // produce: load file
    int len = strlen(ppm_source[1]) + strlen(files[local_file_idx]) + 2;
    char *full_path = malloc(len * sizeof(char));
    assert(full_path != NULL);
    strcpy(full_path, ppm_source[1]);
    strcat(full_path, "/");
    strcat(full_path, files[local_file_idx]);
    FILE *f_in = fopen(full_path, "r");
    assert(f_in != NULL);

    // add to buffer
    int file_order = convert_filename_to_order(files[local_file_idx]) - 1;
    pthread_mutex_lock(&buffer_in_index_mutexes[file_order]);
    buffer_in[file_order].nbytes = fread(
        buffer_in[file_order].data, sizeof(unsigned char), BUFFER_SIZE, f_in);
    fclose(f_in);
    total_in += buffer_in[file_order].nbytes;
    pthread_mutex_unlock(&buffer_in_index_mutexes[file_order]);

    pthread_mutex_lock(&linked_list_mutex);
    push(file_order);
    pthread_mutex_unlock(&linked_list_mutex);

    free(full_path);
    pthread_cond_signal(&cond_consumer);
  }
  return NULL;
}

void *consumer(void *args) {
  for (int i = 0; i < FILE_COUNT / CONSUMER_THREADS_NUM; i++) {
    pthread_mutex_lock(&linked_list_mutex);
    while (head == NULL) { // buffer is empty
      printf("consumer waiting\n");
      pthread_cond_wait(&cond_consumer, &linked_list_mutex);
    }
    int index_to_consume = pop();
    pthread_mutex_unlock(&linked_list_mutex);

    // zip file
    z_stream strm;
    int ret = deflateInit(&strm, 9);
    assert(ret == Z_OK);
    strm.avail_in = buffer_in[index_to_consume].nbytes;
    strm.next_in = buffer_in[index_to_consume].data;
    strm.avail_out = BUFFER_SIZE;
    strm.next_out = buffer_out[index_to_consume].data;

    ret = deflate(&strm, Z_FINISH);
    assert(ret == Z_STREAM_END);

    pthread_mutex_lock(&buffer_out_index_mutexes[index_to_consume]);
    buffer_out[index_to_consume].nbytes = BUFFER_SIZE - strm.avail_out;
    pthread_mutex_unlock(&buffer_out_index_mutexes[index_to_consume]);
    pthread_cond_signal(&cond_producer);
  }
  return NULL;
}

int main(int argc, char **argv) {
  // time computation header
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  // end of time computation header

  // do not modify the main function before this point!

  assert(argc == 2);
  ppm_source = argv;

  DIR *d;
  struct dirent *dir;
  int nfiles = 0;

  d = opendir(argv[1]);
  if (d == NULL) {
    printf("An error has occurred\n");
    return 0;
  }

  // create list of PPM files
  while ((dir = readdir(d)) != NULL) {
    files = realloc(files, (nfiles + 1) * sizeof(char *));
    assert(files != NULL);

    int len = strlen(dir->d_name);
    if (dir->d_name[len - 4] == '.' && dir->d_name[len - 3] == 'p' &&
        dir->d_name[len - 2] == 'p' && dir->d_name[len - 1] == 'm') {
      files[nfiles] = strdup(dir->d_name);
      assert(files[nfiles] != NULL);

      nfiles++;
    }
  }
  closedir(d);
  FILE *f_out = fopen("video.vzip", "w");
  assert(f_out != NULL);

  // create producer and consumer threads
  pthread_t producer_threads[PRODUCER_THREADS_NUM];
  pthread_t consumer_threads[CONSUMER_THREADS_NUM];

  for (int i = 0; i < PRODUCER_THREADS_NUM; i++) {
    if (pthread_create(&producer_threads[i], NULL, producer, NULL) != 0) {
      perror("Failed to create producer thread");
    }
  }
  for (int i = 0; i < CONSUMER_THREADS_NUM; i++) {
    if (pthread_create(&consumer_threads[i], NULL, consumer, NULL) != 0) {
      perror("Failed to create consumer thread");
    }
  }

  for (int i = 0; i < PRODUCER_THREADS_NUM; i++) {
    if (pthread_join(producer_threads[i], NULL) != 0) {
      perror("Failed to join thread");
    }
  }
  for (int i = 0; i < CONSUMER_THREADS_NUM; i++) {
    if (pthread_join(consumer_threads[i], NULL) != 0) {
      perror("Failed to join thread");
    }
  }

  for (int i = 0; i < FILE_COUNT; i++) {
    fwrite(&buffer_out[i].nbytes, sizeof(int), 1, f_out);
    fwrite(buffer_out[i].data, sizeof(unsigned char), buffer_out[i].nbytes,
           f_out);
    total_out += buffer_out[i].nbytes;
  }

  fclose(f_out);
  printf("Compression rate: %.2lf%%\n",
         100.0 * (total_in - total_out) / total_in);

  // release list of files
  for (int i = 0; i < nfiles; i++)
    free(files[i]);
  free(files);

  // do not modify the main function after this point!

  // time computation footer
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Time: %.2f seconds\n",
         ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) -
             ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));
  // end of time computation footer

  return 0;
}