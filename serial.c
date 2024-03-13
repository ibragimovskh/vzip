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


typedef struct {
  unsigned char data[BUFFER_SIZE];
  int nbytes;
  char *filename;
} buffer_t;
buffer_t buffer_in[FILE_COUNT];
buffer_t buffer_out[FILE_COUNT];

int cmp(const void *a, const void *b) {
  buffer_t *bufferA = (buffer_t *)a;
  buffer_t *bufferB = (buffer_t *)b;
  return strcmp(bufferA->filename, bufferB->filename);
}


int count = 0, file_idx = 0;
int total_in = 0, total_out = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t buffer_index_mutexes[FILE_COUNT];
pthread_cond_t cond_producer = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_consumer = PTHREAD_COND_INITIALIZER;


// Define the linked list node structure
typedef struct Node {
    int data;
    struct Node* next;
} Node;

// Function to create a new node
Node* createNode(int data) {
    Node* newNode = (Node*) malloc(sizeof(Node));
    if (!newNode) {
        printf("Error allocating memory for new node\n");
        exit(-1);
    }
    newNode->data = data;
    newNode->next = NULL;
    return newNode;
}

// Function to add a node to the end of the list
void enqueue(Node** head, int data) {
    Node* newNode = createNode(data);
    if (*head == NULL) {
        *head = newNode;
    } else {
        Node* temp = *head;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = newNode;
    }
}

// Function to remove a node from the front of the list
int dequeue(Node** head) {
    if (*head == NULL) {
        printf("List is empty\n");
        return -1;
    }
    Node* temp = *head;
    int data = temp->data;
    *head = (*head)->next;
    free(temp);
    return data;
}

char **ppm_source;
char **files = NULL;
// linked list for jobs
Node* head = NULL;


void *producer(void *args) {
  for (int i = 0; i < FILE_COUNT / PRODUCER_THREADS_NUM; i++) {
    pthread_mutex_lock(&mutex);
    while (count == FILE_COUNT) { // buffer is full
      pthread_cond_wait(&cond_producer, &mutex);
    }

    // create local copies of file_idx and count
    int local_file_idx = file_idx;
    int local_count = count;
    file_idx++;
    count++;

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

    pthread_mutex_lock(&buffer_index_mutexes[local_count]);
    
    // add to buffer
		

    buffer_in[local_count].nbytes = fread(
        buffer_in[local_count].data, sizeof(unsigned char), BUFFER_SIZE, f_in);

		buffer_in[local_count].filename = malloc(strlen(full_path) * sizeof(char));
   
    if (buffer_in[local_count].filename == NULL) {
      perror("Failed to allocate memory");
    }

    strcpy(buffer_in[local_count].filename, full_path);
    fclose(f_in);
    total_in += buffer_in[local_count].nbytes;

		enqueue(&head, local_count);


    pthread_mutex_unlock(&buffer_index_mutexes[local_count]);
    

    free(full_path);
    pthread_cond_signal(&cond_consumer);
  }
  return NULL;
}

void *consumer(void *args) {
  for (int i = 0; i < FILE_COUNT / CONSUMER_THREADS_NUM; i++) {
    pthread_mutex_lock(&mutex);
    while (count == 0) { // buffer is empty
      pthread_cond_wait(&cond_consumer, &mutex);
    }

    // create a local copy of count
    
		int local_count = dequeue(&head);
		count--;
		


    pthread_mutex_unlock(&mutex);

    // zip file
    z_stream strm;
    int ret = deflateInit(&strm, 9);
    assert(ret == Z_OK);
    strm.avail_in = buffer_in[local_count].nbytes;
    strm.next_in = buffer_in[local_count].data;
    strm.avail_out = BUFFER_SIZE;
    strm.next_out = buffer_out[local_count].data;

    ret = deflate(&strm, Z_FINISH);
    assert(ret == Z_STREAM_END);

    pthread_mutex_lock(&buffer_index_mutexes[local_count]);
    buffer_out[local_count].nbytes = BUFFER_SIZE - strm.avail_out;
		// printf("HELLO\n");
		// buffer_out[local_count].filename = malloc(strlen(buffer_in[local_count].filename) * sizeof(char));
		// if (buffer_out[local_count].filename == NULL) {
		// 	perror("Failed to allocate memory");
		// }
		// strcpy(buffer_out[local_count].filename, buffer_in[local_count].filename);
    pthread_mutex_unlock(&buffer_index_mutexes[local_count]);
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


	
	// qsort(buffer_out, FILE_COUNT, sizeof(buffer_t), cmp);

  
	// sort'
	for( int i = 0; i < FILE_COUNT; i++ ) {
		printf("%d\t", i);
		printf("buffer out: %s\n", buffer_out[i].filename);
	}


  for (int i = 0; i < FILE_COUNT; i++) {
    fwrite(&buffer_out[i].nbytes, sizeof(int), 1, f_out);
    fwrite(buffer_out[1].data, sizeof(unsigned char), buffer_out[i].nbytes,
           f_out);
    total_out += buffer_out[i].nbytes;
    free(buffer_out[i].filename);
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