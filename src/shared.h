#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#define MAX_CAR_NAME_LENGTH 255 // Limit for the length of shared memory name
#define SHM_NAME_PREFIX "/car"
#define MAX_FLOOR_LENGTH 4
#define MAX_STATUS_LENGTH 8
#define UP 'U'
#define DOWN 'D'

char *receive_msg(int fd);

int send_message(int fd, const char *msg);

int is_valid_floor(const char *arg);

int are_consecutive_floors(const char *before, const char *after);

int is_floor_within_bounds(const char *floor, const char *lowest_floor, const char *highest_floor);

/**
 * Tokenizes a message into an array of tokens by splitting it on spaces.
 */
void tokenize_message(char *msg, char *tokens[], int max_tokens);

void increment_floor(char *floor);

void decrement_floor(char *floor);

void set_next_floor(char *floor, char direction);

typedef struct {
    pthread_mutex_t mutex;                      // Locked while accessing struct contents
    pthread_cond_t cond;                        // Signalled when the contents change
    char current_floor[MAX_FLOOR_LENGTH];       // C string in the range B99-B1 and 1-999
    char destination_floor[MAX_FLOOR_LENGTH];   // C string in the range B99-B1 and 1-999
    char status[MAX_STATUS_LENGTH];             // C string indicating the elevator's status
    uint8_t open_button;                        // 1 if open doors button is pressed, else 0
    uint8_t close_button;                       // 1 if close doors button is pressed, else 0
    uint8_t door_obstruction;                   // 1 if obstruction detected, else 0
    uint8_t overload;                           // 1 if overload detected
    uint8_t emergency_stop;                     // 1 if stop button has been pressed, else 0
    uint8_t individual_service_mode;            // 1 if in individual service mode, else 0
    uint8_t emergency_mode;                     // 1 if in emergency mode, else 0
} car_shared_mem;
