#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <pthread.h>

#define MAX_CAR_NAME_LENGTH 255 // Limit for the length of shared memory name
#define MAX_FLOOR_LENGTH 4
#define MAX_STATUS_LENGTH 8

typedef struct QueueNode {
    char floor[MAX_FLOOR_LENGTH];   // The floor number
    char direction;                 // 'U' for up, 'D' for down
    struct QueueNode *next;         // Pointer to the next node
} QueueNode;

typedef struct {
    char car_name[MAX_CAR_NAME_LENGTH];         // The name of the car
    char lowest_floor[MAX_FLOOR_LENGTH];        // The lowest floor the car can go to
    char highest_floor[MAX_FLOOR_LENGTH];       // The highest floor the car can go to
    char status[MAX_STATUS_LENGTH];             // The status of the car
    char current_floor[MAX_FLOOR_LENGTH];       // The current floor of the car
    char destination_floor[MAX_FLOOR_LENGTH];   // The destination floor of the car
    int clientfd;                               // The file descriptor of the client
    QueueNode *queue;                           // The head of the linked list of floors
    pthread_mutex_t mutex;                      // Mutex for the shared memory
} Car;

typedef struct car_vector {
	/// The current number of elements in the vector
	size_t size;

	/// The current storage capacity of the vector
	size_t capacity;

	/// The content of the vector.
	Car ** data;

	pthread_mutex_t mutex;
} car_vector_t;

#define CV_INITIAL_CAPACITY 4
#define CV_GROWTH_FACTOR 1.25

void cv_init( car_vector_t *vec );

void cv_destroy( car_vector_t *vec );

size_t cv_size( car_vector_t *vec );

void cv_push( car_vector_t *vec, Car * new_item );

Car * cv_get_at( car_vector_t *vec, size_t index);

void cv_remove( car_vector_t *vec, Car * item );
