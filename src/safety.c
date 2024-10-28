/*
** MISRA C, Safety-Critical Exceptions and Justifications:**

1. **Infinite loop**:
Infinite loop is required since the component is expected to run indefinitely.
The component is expected to run indefinitely to ensure that the car is always in a valid state.

2. **Use of Non-Standard Headers**
Headers such as `pthread.h`, `unistd.h`, `fcntl.h`, `sys/mman.h` are not part of the standard C library.
However, these headers are required for the implementation of the shared memory system and the use of threads.
They are used with:
- the knowledge that the program will be run on a POSIX-compliant system.
- the maximum safety precautions and robust error checkings to ensure that the program is safe and secure.
*/

#include <stdlib.h>
#include <stdint.h>
#include <string.h> 
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define FILE_PERMISSIONS 0666
#define MAX_CAR_NAME_LENGTH 255 // Limit for the length of shared memory name
#define SHM_NAME_PREFIX "/car"

#define MAX_FLOOR_LENGTH 4
#define MAX_STATUS_LENGTH 8

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

car_shared_mem* open_shared_memory(const char * share_name) {
    int fd = shm_open(share_name, O_RDWR, FILE_PERMISSIONS);
    if (fd == -1) {
        return NULL;
    }

    car_shared_mem *shm = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        (void) close(fd);
        return NULL;
    }

    (void) close(fd);

    return shm;
}

/*
* Determines if a character is a digit.
*/
int is_digit(char c) {
    return (c >= '0' && c <= '9') ? 1 : 0;
}

/*
* Validates that the status string contains one of the valid status values.
*/
int validate_status(const char status[MAX_STATUS_LENGTH]) {
    return strncmp(status, "Open", MAX_STATUS_LENGTH) == 0
        || strncmp(status, "Opening", MAX_STATUS_LENGTH) == 0
        || strncmp(status, "Closed", MAX_STATUS_LENGTH) == 0
        || strncmp(status, "Closing", MAX_STATUS_LENGTH) == 0
        || strncmp(status, "Between", MAX_STATUS_LENGTH) == 0;
}

/*
* Validates that the floor string is in the correct format.
* The format can be either "B##" or "###" where # is a digit (no leading zeros).
*/
int validate_floor(const char floor[MAX_FLOOR_LENGTH]) {
    size_t len = strlen(floor);

    // Ensure it's not an empty string
    if (len == 0U) {
        return 0;
    }
    // Check for format: B##
    if (floor[0] == 'B') {
        // Floor number can't start with 0
        if ((len < 2U) || (len > 3U) || (floor[1] == '0')) {
            return 0;
        }

        // Check the remaining characters are digits
        for (size_t i = 1U; i < len; i++) {
            if (is_digit(floor[i]) == 0) {
                return 0;
            }
        }
    }
    // Check for format: ###
    else {
        // Floor number can't start with 0
        if ((len < 1U) || (len > 3U) || (floor[0] == '0')) {
            return 0;
        }

        // Check all characters are digits
        for (size_t i = 0U; i < len; i++) {
            if (is_digit(floor[i]) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

/*
* Validates that all uint8_t values are either 0 or 1.
*/
int validate_bools(car_shared_mem *shm) {
    return shm->open_button <= 1
        && shm->close_button <= 1
        && shm->door_obstruction <= 1
        && shm->overload <= 1
        && shm->emergency_stop <= 1
        && shm->individual_service_mode <= 1
        && shm->emergency_mode <= 1;
}

int validate_door_obstruction(car_shared_mem *shm) {
    return shm->door_obstruction == 0 || strncmp(shm->status, "Opening", MAX_STATUS_LENGTH) == 0 || strncmp(shm->status, "Closing", MAX_STATUS_LENGTH) == 0;
}

/*
* Ensures that everything looks reasonable in the shared memory.
* If something is wrong, it will take appropriate action.
* Returns 1 if a change was made, 0 otherwise.
*/
int check_safety(car_shared_mem *shm) {
    int change_occurred = 0;
    // Check for door obstruction
    if (shm->door_obstruction == 1 && strncmp(shm->status, "Closing", MAX_STATUS_LENGTH) == 0) {
        (void) strncpy(shm->status, "Opening", MAX_STATUS_LENGTH);
        change_occurred = 1;
    }
    // Check for emergency stop
    if (shm->emergency_stop == 1 && shm->emergency_mode == 0) {
        const char *msg = "The emergency stop button has been pressed!\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
        shm->emergency_mode = 1;
        change_occurred = 1;
    }
    // Check for overload
    if (shm->overload == 1 && shm->emergency_mode == 0) {
        const char *msg = "The overload sensor has been tripped!\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
        shm->emergency_mode = 1;
        change_occurred = 1;
    }
    // Validate data consistency
    if (shm->emergency_mode != 1 && (
        !validate_floor(shm->current_floor) ||
        !validate_floor(shm->destination_floor) ||
        !validate_status(shm->status) ||
        !validate_bools(shm) ||
        !validate_door_obstruction(shm))
    ) {
        const char *msg = "Data consistency error!\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
        shm->emergency_mode = 1;
        change_occurred = 1;
    }
    return change_occurred;
}

/*
* Monitors the shared memory for safety issues.
*/
void monitor_safety(car_shared_mem *shm) {
    if (pthread_mutex_lock(&shm->mutex) != 0) {
        const char *msg = "Error locking mutex!\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
    }
    if (pthread_cond_wait(&shm->cond, &shm->mutex) != 0) {
        const char *msg = "Error waiting on condition variable!\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
    }

    int change_occurred = check_safety(shm);
    // There was a change in the shared memory -> broadcast the condition variable
    if (change_occurred) {
        if (pthread_cond_broadcast(&shm->cond) != 0) {
            const char *msg = "Error broadcasting condition variable!\n";
            (void) write(STDOUT_FILENO, msg, strlen(msg));
        }
    }

    if (pthread_mutex_unlock(&shm->mutex) != 0) {
        const char *msg = "Error unlocking mutex!\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
    }
}

int main(int argc, char **argv) {
    // Check if exactly 1 argument is passed
    if (argc != 2) {
        const char *msg = "Usage: safety {car name}\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
        exit(EXIT_FAILURE);
    }

    // Calculate the length of the car name and ensure it doesn't exceed the limit
    size_t car_name_len = strlen(argv[1]);
    size_t prefix_len = strlen(SHM_NAME_PREFIX);

    if (car_name_len + prefix_len >= MAX_CAR_NAME_LENGTH) {
        const char *msg = "Car name too long.\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
        exit(EXIT_FAILURE);
    }

    char share_name[MAX_CAR_NAME_LENGTH];
    (void) strncpy(share_name, SHM_NAME_PREFIX, prefix_len + 1);                // Copy "/car" (including null terminator)
    (void) strncat(share_name, argv[1], MAX_CAR_NAME_LENGTH - prefix_len - 1);  // Concatenate car name

    car_shared_mem *shm = open_shared_memory(share_name);

    if (shm == NULL) {
        const char *msg = "Unable to access car ";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
        (void) write(STDOUT_FILENO, argv[1], strlen(argv[1]));
        msg = ".\n";
        (void) write(STDOUT_FILENO, msg, strlen(msg));
        exit(EXIT_FAILURE);
    }

    for ( ; ; ) {
        monitor_safety(shm);
    }

    exit(EXIT_SUCCESS);
}
