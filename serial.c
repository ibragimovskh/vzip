#include <dirent.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <pthread.h>

#define BUFFER_SIZE 1048576 // 1MB
#define MAX_THREADS 20
#define NUM_PRODUCERS 10
#define NUM_CONSUMERS 10

typedef struct {
  z_stream * strm;
  unsigned char buffer_in[BUFFER_SIZE];
  unsigned char buffer_out[BUFFER_SIZE];
  int nbytes;
}
thread_args;

pthread_t p[NUM_PRODUCERS];
pthread_t c[NUM_CONSUMERS];

pthread_mutex_t lock;
pthread_cond_t full, empty;

int cmp(const void * a,
  const void * b) {
  return strcmp( * (char ** ) a, *(char ** ) b);
}

void * producer(void * args) {
  thread_args * ta = (thread_args * ) args;
  pthread_mutex_lock(&lock);

   while(ta -> nbytes == BUFFER_SIZE) {
		printf("waiting for full\n");
    pthread_cond_wait( &empty, &lock);
  }

  // size of next_in
  z_stream * stream = ta -> strm;
	assert(stream);
  int ret = deflateInit(stream, 9);
  assert(ret == Z_OK);
  // pointer to the next byte to be read (WHERE to read from)
  stream -> next_in = ta -> buffer_in;
  stream -> avail_in = ta -> nbytes;
  pthread_cond_signal( &full);
  pthread_mutex_unlock( & lock);
  return NULL;
}

void * consumer(void * args) {
  thread_args * ta = (thread_args * ) args;
 
  pthread_mutex_lock(&lock);

  while(ta -> nbytes == 0) {
		printf("waiting for full\n");
    pthread_cond_wait(&full, &lock);
  }

	FILE * f_out = fopen("video.vzip", "a");
  assert(f_out != NULL);

  z_stream * stream = ta -> strm;
  stream -> avail_out = BUFFER_SIZE;

  // pointer to the next byte to be written (WHERE to write to)
  stream -> next_out = ta -> buffer_out;
  int ret = deflate(stream, Z_FINISH);
  assert(ret == Z_STREAM_END);
  int nbytes_zipped = BUFFER_SIZE - (stream -> avail_out);
  fwrite( & nbytes_zipped, sizeof(int), 1, f_out);
  fwrite(ta -> buffer_out, sizeof(unsigned char), nbytes_zipped, f_out);
	fflush(f_out);

  pthread_cond_signal(&empty);
  pthread_mutex_unlock(&lock);

  fclose(f_out);
  return NULL;
}

int main(int argc, char ** argv) {
  // time computation header
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, & start);
  // end of time computation header

  // do not modify the main function before this point!

  assert(argc == 2);

  FILE * file = fopen("video.vzip", "w");
  if (file == NULL) {
    printf("Error opening file\n");
    return 1;
  }
  fclose(file);

  DIR * d;
  struct dirent * dir;
  char ** files = NULL;
  int nfiles = 0;
  d = opendir(argv[1]);
  if (d == NULL) {
    printf("An error has occurred\n");
    return 0;
  }

  // create sorted list of PPM files
  while ((dir = readdir(d)) != NULL) {
    files = realloc(files, (nfiles + 1) * sizeof(char * ));
    assert(files != NULL);
    // length of the file name
    int len = strlen(dir -> d_name);
    printf("File name: %s\n", dir -> d_name);
    if (dir -> d_name[len - 4] == '.' && dir -> d_name[len - 3] == 'p' && dir -> d_name[len - 2] == 'p' && dir -> d_name[len - 1] == 'm') {
      // strdup is a safety precaution to avoid memory leaks
      files[nfiles] = strdup(dir -> d_name);
      assert(files[nfiles] != NULL);
      // increment the number of ppm files
      nfiles++;
    }
  }
  closedir(d);

  //why sort them?

  // LOCK
  pthread_mutex_init( &lock, NULL);

  // create a single zipped package with all PPM files in lexicographical order
  int total_in = 0, total_out = 0;
  FILE * f_out = fopen("video.vzip", "w");
  assert(f_out != NULL);
	int nbytes[100];
	

  for (int i = 0; i < nfiles; i++) {
    // directory path + / + file name + null terminator
    int len = strlen(argv[1]) + strlen(files[i]) + 2;
    char * full_path = malloc(len * sizeof(char));

    assert(full_path != NULL);

    strcpy(full_path, argv[1]);
    strcat(full_path, "/");
    strcat(full_path, files[i]);

    unsigned char buffer_in[BUFFER_SIZE];
    unsigned char buffer_out[BUFFER_SIZE];

    // load file
    FILE * f_in = fopen(full_path, "r");
    assert(f_in != NULL);
    /**
     * Reads data from the file pointed to by f_in and stores it in the buffer_in array.
     * 
     * @param buffer_in The array to store the read data.
     * @param sizeof(unsigned char) The size of each element in the buffer_in array.
     * @param BUFFER_SIZE The size of the buffer_in array.
     * @param f_in The file pointer to the input file.
     * @return The number of bytes read from the file.
     */
    nbytes[i] = fread(buffer_in, sizeof(unsigned char), BUFFER_SIZE, f_in);
    fclose(f_in);
    total_in += nbytes[i];

    free(full_path);
  }

	
  thread_args ta[NUM_PRODUCERS];

		
  for (int i = 0; i < NUM_PRODUCERS; i++) {
    ta[i].strm = malloc(sizeof(z_stream));
    ta[i].nbytes = nbytes[i];
    if (pthread_create( &p[i], NULL, &producer, &ta[i]) != 0) {
      perror("Failed to create a producer thread\n");
    }

    if (pthread_create( &c[i], NULL, &consumer, &ta[i]) != 0) {
      perror("Failed to create a consumer thread\n");
    }
  }

  for (int i = 0; i < NUM_PRODUCERS; i++) {
    if (pthread_join(p[i], NULL) != 0) {
      perror("Failed to join a producer thread\n");
    }
  
  //printf("file here");

    if (pthread_join(c[i], NULL) != 0) {
      perror("Failed to join a consumer thread\n"); 
  	}
	}
  // fclose(f_out);
  pthread_mutex_destroy(&lock);
  pthread_cond_destroy(&full);
  pthread_cond_destroy(&empty);

  qsort(files, nfiles, sizeof(char * ), cmp);

  printf("Compression rate: %.2lf%%\n", 100.0 * (total_in - total_out) / total_in);

  // release list of files
  for (int i = 0; i < nfiles; i++)
    free(files[i]);
  free(files);
  // do not modify the main function after this point!

  // time computation footer
  clock_gettime(CLOCK_MONOTONIC, & end);
  printf("Time: %.2f seconds\n", ((double) end.tv_sec + 1.0e-9 * end.tv_nsec) - ((double) start.tv_sec + 1.0e-9 * start.tv_nsec));
  // end of time computation footer

  return 0;
}

// okay, so the way to optimize this is to use threads to compress the files in parallel
// files don't have to be sorted prior, but need to be sorted after 
// we need to wait for all threads to finish before sorting the files
// we need to use a mutex to lock the file writing process
// need to use conditional variables, to avoid buffer overflow/underflow
// signling is when a thread sends a signal to another thread (they collaborate)

// what if we have two producers and one consumer?
// we need to use a mutex to lock the buffer, avoid race condition (two threads writing to the same buffer)
// two producers would fill the buffer faster than the consumer can consume it
// that's why we need condition variables to check whether the buffer is full or empty

// same for opposite≠≠–