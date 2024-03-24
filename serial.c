#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#define PRODUCER_THREADS_NUM 8
#define CONSUMER_THREADS_NUM 11

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t linked_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t producer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t consumer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_producer = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_consumer = PTHREAD_COND_INITIALIZER;

typedef struct {
  unsigned char *data;
  int nbytes;
} buffer_t;

typedef struct {
  buffer_t *buffer_in;
  buffer_t *buffer_out;
  pthread_mutex_t *buffer_in_index_mutexes;
  pthread_mutex_t *buffer_out_index_mutexes;
} thread_args_t;

typedef struct node {
  int index_to_consume;
  struct node *next;
} node_t;

char **ppm_source;
char **files = NULL;
char *full_path = NULL;
int full_path_len = 0;
node_t *head = NULL;
int count = 0, file_idx = 0;
int total_in = 0, total_out = 0;
int buffer_size = 0;
int nfiles = 0;
int prod_regular_work = 0, prod_remainder_work = 0;
int cons_regular_work = 0, cons_remainder_work = 0;
int first_producer = 1, first_consumer = 1;

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

void *producer(void *args) {
  thread_args_t *thread_args = (thread_args_t *)args;
  pthread_mutex_lock(&producer_mutex);
  int work_size = first_producer ? prod_remainder_work : prod_regular_work;
  first_producer = 0;
  pthread_mutex_unlock(&producer_mutex);

  for (int i = 0; i < work_size; i++) {
    pthread_mutex_lock(&mutex);
    while (file_idx == nfiles) { // buffer is full
      pthread_cond_wait(&cond_producer, &mutex);
    }
    // create local copies of file_idx and count
    int local_file_idx = file_idx;
    file_idx++;
    pthread_mutex_unlock(&mutex);

    // produce: load file
    char temp_buffer[full_path_len];
    strcpy(temp_buffer, full_path);
    strcat(temp_buffer, files[local_file_idx]);
    FILE *f_in = fopen(temp_buffer, "r");
    assert(f_in != NULL);

    // add to buffer
    int file_order = convert_filename_to_order(files[local_file_idx]) - 1;
    pthread_mutex_lock(&thread_args->buffer_in_index_mutexes[file_order]);
    thread_args->buffer_in[file_order].nbytes =
        fread(thread_args->buffer_in[file_order].data, sizeof(unsigned char),
              buffer_size, f_in);
    fclose(f_in);
    pthread_mutex_unlock(&thread_args->buffer_in_index_mutexes[file_order]);
    pthread_mutex_lock(&linked_list_mutex);
    push(file_order);
    pthread_mutex_unlock(&linked_list_mutex);
    pthread_cond_signal(&cond_consumer);
  }
  return NULL;
}

void *consumer(void *args) {
  thread_args_t *thread_args = (thread_args_t *)args;
  pthread_mutex_lock(&consumer_mutex);
  int work_size = first_consumer ? cons_remainder_work : cons_regular_work;
  first_consumer = 0;
  pthread_mutex_unlock(&consumer_mutex);
  for (int i = 0; i < work_size; i++) {
    pthread_mutex_lock(&linked_list_mutex);
    while (head == NULL) { // buffer is empty
      pthread_cond_wait(&cond_consumer, &linked_list_mutex);
    }
    int index_to_consume = pop();
    pthread_mutex_unlock(&linked_list_mutex);

    // zip file
    z_stream strm;
    int ret = deflateInit(&strm, 9);
    assert(ret == Z_OK);
    strm.avail_in = thread_args->buffer_in[index_to_consume].nbytes;
    strm.next_in = thread_args->buffer_in[index_to_consume].data;
    strm.avail_out = buffer_size;
    strm.next_out = thread_args->buffer_out[index_to_consume].data;
    ret = deflate(&strm, Z_FINISH);
    assert(ret == Z_STREAM_END);

    pthread_mutex_lock(
        &thread_args->buffer_out_index_mutexes[index_to_consume]);
    thread_args->buffer_out[index_to_consume].nbytes =
        buffer_size - strm.avail_out;
    pthread_mutex_unlock(
        &thread_args->buffer_out_index_mutexes[index_to_consume]);
    pthread_cond_signal(&cond_producer);
  }
  return NULL;
}

void initialize_thread_args(thread_args_t *thread_args, int nfiles,
                            int buffer_size) {
  thread_args->buffer_in = malloc(nfiles * sizeof(buffer_t));
  thread_args->buffer_out = malloc(nfiles * sizeof(buffer_t));
  thread_args->buffer_in_index_mutexes =
      malloc(nfiles * sizeof(pthread_mutex_t));
  thread_args->buffer_out_index_mutexes =
      malloc(nfiles * sizeof(pthread_mutex_t));

  for (int i = 0; i < nfiles; i++) {
    thread_args->buffer_in[i].data = malloc(buffer_size);
    thread_args->buffer_out[i].data = malloc(buffer_size);
    thread_args->buffer_in[i].nbytes = 0;
    thread_args->buffer_out[i].nbytes = 0;
    pthread_mutex_init(&thread_args->buffer_in_index_mutexes[i], NULL);
    pthread_mutex_init(&thread_args->buffer_out_index_mutexes[i], NULL);
  }
  assert(thread_args->buffer_in_index_mutexes != NULL &&
         thread_args->buffer_out_index_mutexes != NULL &&
         thread_args->buffer_in != NULL && thread_args->buffer_out != NULL);
}

void cleanup(thread_args_t *thread_args, int nfiles) {
  for (int i = 0; i < nfiles; i++) {
    free(thread_args->buffer_in[i].data);
    free(thread_args->buffer_out[i].data);
  }
  free(thread_args->buffer_in);
  free(thread_args->buffer_out);
  free(thread_args->buffer_in_index_mutexes);
  free(thread_args->buffer_out_index_mutexes);
  free(full_path);
  for (int i = 0; i < nfiles; i++)
    free(files[i]);
  free(files);
}

int get_filesize() {
  int len = strlen(ppm_source[1]) + strlen(files[0]) + 2;
  char *full_path = malloc(len * sizeof(char));
  assert(full_path != NULL);
  strcpy(full_path, ppm_source[1]);
  strcat(full_path, "/");
  strcat(full_path, files[0]);
  FILE *f = fopen(full_path, "r");
  assert(f != NULL);
  fseek(f, 0, SEEK_END);
  int size = ftell(f);
  fclose(f);
  return size;
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

  buffer_size = get_filesize();
  total_in = nfiles * buffer_size;
  prod_regular_work = (int)ceil((double)nfiles / PRODUCER_THREADS_NUM);
  prod_remainder_work =
      nfiles - (prod_regular_work * (PRODUCER_THREADS_NUM - 1));
  cons_regular_work = (int)ceil((double)nfiles / CONSUMER_THREADS_NUM);
  cons_remainder_work =
      nfiles - (cons_regular_work * (CONSUMER_THREADS_NUM - 1));

  full_path_len = strlen(ppm_source[1]) + strlen(files[0]) + 2;
  full_path = (char *)malloc(full_path_len * sizeof(char));
  assert(full_path != NULL);
  strcpy(full_path, ppm_source[1]);
  strcat(full_path, "/");

  // create producer and consumer threads
  pthread_t producer_threads[PRODUCER_THREADS_NUM];
  pthread_t consumer_threads[CONSUMER_THREADS_NUM];

  thread_args_t thread_args;
  initialize_thread_args(&thread_args, nfiles, buffer_size);

  for (int i = 0; i < PRODUCER_THREADS_NUM; i++) {
    if (pthread_create(&producer_threads[i], NULL, producer,
                       (void *)&thread_args) != 0) {
      perror("Failed to create producer thread");
    }
  }
  for (int i = 0; i < CONSUMER_THREADS_NUM; i++) {
    if (pthread_create(&consumer_threads[i], NULL, consumer,
                       (void *)&thread_args) != 0) {
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

  for (int i = 0; i < nfiles; i++) {
    fwrite(&thread_args.buffer_out[i].nbytes, sizeof(int), 1, f_out);
    fwrite(thread_args.buffer_out[i].data, sizeof(unsigned char),
           thread_args.buffer_out[i].nbytes, f_out);
    total_out += thread_args.buffer_out[i].nbytes;
  }
  fclose(f_out);
  printf("Compression rate: %.2lf%%\n",
         100.0 * (total_in - total_out) / total_in);

  // release list of files
  cleanup(&thread_args, nfiles);

  // do not modify the main function after this point!

  // time computation footer
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Time: %.2f seconds\n",
         ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) -
             ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));
  // end of time computation footer

  return 0;
}