#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "shared.h"

#define MILLISECOND 1000 // 1ms

static volatile int keep_running = 1;

typedef struct {
    char *name;
    char *lowest_floor;
    char *highest_floor;
    int delay;
    int sockfd; // Controller socket
    int should_connect; // 1 if the car should connect to the controller, else 0
    car_shared_mem *shm;
} car_data;

void handle_sigint(int dummy) {
    keep_running = 0;
}

car_shared_mem * create_shared_memory(const char *share_name, const char *init_floor) {
    // Create the shared memory object, allowing read-write access
    int fd = shm_open(share_name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open()");
        return NULL;
    }

    // Set the capacity of the shared memory object via ftruncate
    if (ftruncate(fd, sizeof(car_shared_mem)) == -1) {
        perror("ftruncate()");
        close(fd);
        return NULL;
    }

    // Map the shared memory via mmap and save the address
    car_shared_mem* shm = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap()");
        close(fd);
        return NULL;
    }

    close(fd);

    // Initialize mutex and condition variable
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shm->cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    // Initialize other fields
    strcpy(shm->current_floor, init_floor);
    strcpy(shm->destination_floor, init_floor);
    strcpy(shm->status, "Closed");
    shm->open_button = 0;
    shm->close_button = 0;
    shm->door_obstruction = 0;
    shm->overload = 0;
    shm->emergency_stop = 0;
    shm->individual_service_mode = 0;
    shm->emergency_mode = 0;

    return shm;
}

void destroy_shared_memory(car_shared_mem* shm, const char *share_name) {
    pthread_mutex_destroy(&shm->mutex);
    pthread_cond_destroy(&shm->cond);
    
    if (munmap(shm, sizeof(car_shared_mem)) == -1) {
        perror("munmap");
    }

    if (shm_unlink(share_name) == -1) {
        perror("shm_unlink");
    }
}

struct timespec get_timeout(int delay) {
    struct timespec timeout;
    // Get current time and add car_info->delay seconds to it
    clock_gettime(CLOCK_REALTIME, &timeout);
    // Add delay in milliseconds, converting to seconds and nanoseconds
    timeout.tv_sec += delay / MILLISECOND;                 // Convert milliseconds to seconds
    timeout.tv_nsec += (delay % MILLISECOND) * 1000000;    // Convert remaining milliseconds to nanoseconds
    // Handle potential overflow in nanoseconds
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
    }
    return timeout;
}

void * controller_send(void *arg) {
    car_data *car_info = (car_data *) arg;
    // Send: CAR {name} {lowest floor} {highest floor}
    // CAR (3) + space (1) + CAR NAME (255) + space (1) + lowest floor (3) + space (1) + highest floor (3) + null terminator (1)
    char initial_msg[268] = {0};
    snprintf(initial_msg, 268, "CAR %s %s %s", car_info->name, car_info->lowest_floor, car_info->highest_floor);

    if (send_message(car_info->sockfd, initial_msg)) {
        perror("129: send_message()");
        pthread_exit(NULL);
    }

    char last_status[8] = {0};
    char last_curr_floor[4] = {0};
    char last_dest_floor[4] = {0};

    // STATUS (6) + space (1) + status (7) + space (1) + current_floor (3) + space (1) + destination_floor (3) + null terminator (1)
    char status_msg[23] = {0};

    while (car_info->should_connect && keep_running) {
        struct timespec timeout = get_timeout(car_info->delay);

        pthread_mutex_lock(&car_info->shm->mutex);
        // Wait for changes in the shared memory or timeout
        while (strcmp(last_status, car_info->shm->status) == 0
            && strcmp(last_curr_floor, car_info->shm->current_floor) == 0
            && strcmp(last_dest_floor, car_info->shm->destination_floor) == 0) {
            int ret = pthread_cond_timedwait(&car_info->shm->cond, &car_info->shm->mutex, &timeout);

            // Break if timed out
            if (ret == ETIMEDOUT) {
                break;
            }
        }
        // Check if the thread should stop
        if (!car_info->should_connect || !keep_running) {
            pthread_mutex_unlock(&car_info->shm->mutex);
            break;
        }

        // Copy the current values to last values
        strcpy(last_status, car_info->shm->status);
        strcpy(last_curr_floor, car_info->shm->current_floor);
        strcpy(last_dest_floor, car_info->shm->destination_floor);
        pthread_mutex_unlock(&car_info->shm->mutex);

        // Send: STATUS {status} {current floor} {destination floor}
        sprintf(status_msg, "STATUS %s %s %s", car_info->shm->status, car_info->shm->current_floor, car_info->shm->destination_floor);
        
        // Sending the status message failed -> stop the thread
        if (send_message(car_info->sockfd, status_msg) == -1) {
            perror("175: send_message()");
            pthread_exit(NULL);
        }
    }

    pthread_mutex_lock(&car_info->shm->mutex);

    if (car_info->shm->individual_service_mode == 1) {
        send_message(car_info->sockfd, "INDIVIDUAL SERVICE");
    }
    else if (car_info->shm->emergency_mode == 1) {
        send_message(car_info->sockfd, "EMERGENCY");
    }

    pthread_mutex_unlock(&car_info->shm->mutex);

    pthread_exit(NULL);
}

void cleanup_mutex_unlock(void *arg) {
    pthread_mutex_unlock((pthread_mutex_t *) arg);
}

void * controller_receive(void *arg) {
    car_data *car_info = (car_data *) arg;

    while (keep_running) {
        char *msg = receive_msg(car_info->sockfd);
        // Receiving the message failed -> try again
        if (msg == NULL) {
            perror("receive_msg()");
            continue;
        }

        char *tokens[4];
        tokenize_message(msg, tokens, 4);

        // Check if the message is in valid format: FLOOR {floor}, else ignore it
        if (strncmp(tokens[0], "FLOOR", 5) == 0) {
            pthread_mutex_lock(&car_info->shm->mutex);
            // Push the cleanup handler in case the thread gets canceled
            pthread_cleanup_push(cleanup_mutex_unlock, &car_info->shm->mutex);

            // The destination floor is the same as the current floor -> open the doors
            if (strcmp(car_info->shm->current_floor, tokens[1]) == 0) {
                car_info->shm->open_button = 1;
            }
            // Set the destination floor to the desired floor
            else {
                strcpy(car_info->shm->destination_floor, tokens[1]);
            }

            pthread_cond_broadcast(&car_info->shm->cond);
            // Pop the cleanup handler and execute it
            pthread_cleanup_pop(1);
        }
    }

    pthread_exit(NULL);
}

void * controller_connect(void *arg) {
    car_data *car_info = (car_data *) arg;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    const char *ipaddress = "127.0.0.1";
    if (inet_pton(AF_INET, ipaddress, &addr.sin_addr) != 1) {
        fprintf(stderr, "inet_pton(%s)\n", ipaddress);
        pthread_exit(NULL);
    }

    while (car_info->should_connect && keep_running) {
        car_info->sockfd = socket(AF_INET, SOCK_STREAM, 0);

        // Attempt to connect to the elevator system every car_info->delay milliseconds
        if (connect(car_info->sockfd, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
            usleep(car_info->delay * MILLISECOND);
            close(car_info->sockfd);
            continue;
        }

        // The connection with the controller should not be established
        if (!car_info->should_connect || !keep_running) {
            break;
        }

        // Create a new thread which will be responsible for sending messages to the controller
        pthread_t send_thread_id;
        pthread_create(&send_thread_id, NULL, controller_send, (void *) car_info);
        // Create a new thread which will be responsible for receiving messages from the controller
        pthread_t recieve_thread_id;
        pthread_create(&recieve_thread_id, NULL, controller_receive, (void *) car_info);
        // The sending thread was cancelled -> cancel also the receiving thread
        pthread_join(send_thread_id, NULL);
        pthread_cancel(recieve_thread_id);
        pthread_join(recieve_thread_id, NULL);
        // Close the socket
        close(car_info->sockfd);
        
        // The connection with the controller should not be re-established
        if (!car_info->should_connect || !keep_running) {
            break;
        }
    }

    pthread_exit(NULL);
}

int controller_init(car_data *car_info) {
    pthread_t thread_id;
    int thread_create_result = pthread_create(&thread_id, NULL, controller_connect, (void *) car_info);
    if (thread_create_result != 0) {
        fprintf(stderr, "pthread_create() failed: %s\n", strerror(thread_create_result));
        return -1;
    }

    // Detach the thread to allow it to clean up automatically when it's done
    pthread_detach(thread_id);
    return 0;
}

void close_doors(car_data *car_info);

void open_doors(car_data *car_info) {
    // Keep trying opening the doors until they are open
    while (strcmp(car_info->shm->status, "Open") != 0) {
        // The doors are closed or closing -> open them
        if (strcmp(car_info->shm->status, "Closed") == 0 || strcmp(car_info->shm->status, "Closing") == 0) {
            strcpy(car_info->shm->status, "Opening");
            pthread_cond_broadcast(&car_info->shm->cond);
            // Simulate the delay of opening the doors
            struct timespec timeout = get_timeout(car_info->delay);
            while (pthread_cond_timedwait(&car_info->shm->cond, &car_info->shm->mutex, &timeout) != ETIMEDOUT) {
                // The close button was pressed -> stop the opening action and close the doors
                if (car_info->shm->close_button == 1) {
                    close_doors(car_info);
                    return;
                }
            }
        }
        // The doors are still opening -> open them
        if (strcmp(car_info->shm->status, "Opening") == 0) {
            strcpy(car_info->shm->status, "Open");
            pthread_cond_broadcast(&car_info->shm->cond);
        }
    }
    // No individual service or emergency mode -> let the doors open for delay
    if (car_info->shm->individual_service_mode == 0 && car_info->shm->emergency_mode == 0) {
        // Simulate the delay of opening the doors
        struct timespec timeout = get_timeout(car_info->delay);
        while (pthread_cond_timedwait(&car_info->shm->cond, &car_info->shm->mutex, &timeout) != ETIMEDOUT) {
            // The close button was pressed -> close the doors immediately
            if (car_info->shm->close_button == 1) {
                close_doors(car_info);
                return;
            }
        }
    }
    // No individual service or emergency mode and the doors are still open -> close them
    if (car_info->shm->individual_service_mode == 0 && car_info->shm->emergency_mode == 0 && strcmp(car_info->shm->status, "Open") == 0) {
        close_doors(car_info);
    }
}

void close_doors(car_data *car_info) {
    // Keep trying closing the doors until they are closed
    while (strcmp(car_info->shm->status, "Closed") != 0) {
        // The doors are open or opening -> close them
        if (strcmp(car_info->shm->status, "Open") == 0 || strcmp(car_info->shm->status, "Opening") == 0) {
            strcpy(car_info->shm->status, "Closing");
            pthread_cond_broadcast(&car_info->shm->cond);
            // Simulate the delay of closing the doors
            struct timespec timeout = get_timeout(car_info->delay);
            while (pthread_cond_timedwait(&car_info->shm->cond, &car_info->shm->mutex, &timeout) != ETIMEDOUT) {
                // The open button was pressed -> stop the opening action and open the doors
                if (car_info->shm->open_button == 1) {
                    open_doors(car_info);
                    return;
                }
            }
        }
        // The doors are still closing -> close them
        if (strcmp(car_info->shm->status, "Closing") == 0) {
            strcpy(car_info->shm->status, "Closed");
            pthread_cond_broadcast(&car_info->shm->cond);
        }
    }
}

void move_car(car_data *car_info) {
    // Destination floor is out of bounds -> set it to the current floor
    if (!is_floor_within_bounds(car_info->shm->destination_floor, car_info->lowest_floor, car_info->highest_floor)) {
        strcpy(car_info->shm->destination_floor, car_info->shm->current_floor);
        return;
    }

    char direction = are_consecutive_floors(car_info->shm->current_floor, car_info->shm->destination_floor) ? UP : DOWN;

    // Move the car floor-by-floor until the destination is reached
    while (strcmp(car_info->shm->current_floor, car_info->shm->destination_floor) != 0) {
        // Set status to "Between" while moving
        strcpy(car_info->shm->status, "Between");
        pthread_cond_broadcast(&car_info->shm->cond);
        pthread_mutex_unlock(&car_info->shm->mutex);
        usleep(car_info->delay * MILLISECOND);
        pthread_mutex_lock(&car_info->shm->mutex);
        // Adjust the floor (increment or decrement)
        set_next_floor(car_info->shm->current_floor, direction);
    }
    // Reset button states
    car_info->shm->open_button = car_info->shm->close_button = 0;

    strcpy(car_info->shm->status, "Closed");
    pthread_cond_broadcast(&car_info->shm->cond);
}

void manage_car(car_data *car_info) {
    // Initialize connection with the controller
    controller_init(car_info);

    int last_individual_service_mode = 0;
    int last_emergency_mode = 0;

    while (keep_running) {
        pthread_mutex_lock(&car_info->shm->mutex);
        // Wait until a change in the shared memory occurs
        struct timespec timeout = get_timeout(car_info->delay);
        pthread_cond_timedwait(&car_info->shm->cond, &car_info->shm->mutex, &timeout);

        if (car_info->shm->open_button == 1) {
            car_info->shm->open_button = 0;
            open_doors(car_info);
        }
        if (car_info->shm->close_button == 1) {
            car_info->shm->close_button = 0;
            close_doors(car_info);
        }

        if (car_info->shm->individual_service_mode == 0 && last_individual_service_mode == 1) {
            car_info->should_connect = 1;
            controller_init(car_info);
        }

        if (car_info->shm->emergency_mode == 0 && last_emergency_mode == 1) {
            car_info->should_connect = 1;
            controller_init(car_info);
        }

        if (car_info->shm->emergency_mode == 1) {
            car_info->should_connect = 0;
        }

        if (car_info->shm->individual_service_mode == 1) {
            if (last_individual_service_mode == 0) {
                car_info->should_connect = 0;
            }
            if (strcmp(car_info->shm->current_floor, car_info->shm->destination_floor) != 0) {
                move_car(car_info);
            }
        }

        if (car_info->shm->individual_service_mode == 0 && car_info->shm->emergency_mode == 0) {
            // The destination floor is different from the current floor and the doors are closed
            if (strcmp(car_info->shm->current_floor, car_info->shm->destination_floor) != 0 && strcmp(car_info->shm->status, "Closed") == 0) {
                move_car(car_info);
                open_doors(car_info);
            }
        }

        last_individual_service_mode = car_info->shm->individual_service_mode;
        last_emergency_mode = car_info->shm->emergency_mode;
        pthread_mutex_unlock(&car_info->shm->mutex);
    }
}

int main(int argc, char **argv) {
    // Check if exactly 4 arguments are passed
    if (argc != 5) {
        printf("Usage: %s {name} {lowest floor} {highest floor} {delay}\n", argv[0]);
        exit(1);
    }
    char * car_name = argv[1];
    char * lowest_floor = argv[2];
    char * highest_floor = argv[3];

    // Check if the floor numbers are valid
    if (!is_valid_floor(lowest_floor) || !is_valid_floor(highest_floor) || !are_consecutive_floors(lowest_floor, highest_floor)) {
        printf("Invalid floor(s) specified.\n");
        exit(1);
    }

    // Check if the delay is valid
    char *p = NULL;
    errno = 0;
    long conv = strtol(argv[4], &p, 10);

    if (errno != 0 || *p != '\0' || conv > INT_MAX || conv < INT_MIN) {
        printf("Invalid delay specified.\n");
        exit(1);
    }
    int delay = conv;

    // Calculate the length of the car name and ensure it doesn't exceed the limit
    size_t car_name_len = strlen(car_name);
    size_t prefix_len = strlen(SHM_NAME_PREFIX);

    if (car_name_len + prefix_len >= MAX_CAR_NAME_LENGTH) {
        printf("Car name too long.\n");
        exit(EXIT_FAILURE);
    }

    char share_name[MAX_CAR_NAME_LENGTH];
    (void) strncpy(share_name, SHM_NAME_PREFIX, prefix_len + 1);                // Copy "/car" (including null terminator)
    (void) strncat(share_name, car_name, MAX_CAR_NAME_LENGTH - prefix_len - 1);  // Concatenate car name

    car_shared_mem *shm = create_shared_memory(share_name, lowest_floor);

    if (shm == NULL) {
        exit(1);
    }

    car_data *car_info = malloc(sizeof(car_data));
    car_info->name = car_name;
    car_info->lowest_floor = lowest_floor;
    car_info->highest_floor = highest_floor;
    car_info->delay = delay;
    car_info->should_connect = 1;
    car_info->shm = shm;

    // Don't terminate the program when writing to a closed socket
    signal(SIGPIPE, SIG_IGN);
    // Register signal handler for SIGINT (Ctrl + C)
    signal(SIGINT, handle_sigint);

    manage_car(car_info);

    destroy_shared_memory(shm, share_name);
    free(car_info);
}
