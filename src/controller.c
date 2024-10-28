#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <unistd.h>
#include "shared.h"
#include "car_vector.h"

#define MAX_CLIENTS 10

int listensockfd;  // Global variable for the listening socket
car_vector_t cars; // Global variable for the cars vector

// Signal handler for SIGINT
void handle_sigint(int dummy) {
    if (close(listensockfd) == -1) {
        perror("close() failed");
    }
    cv_destroy(&cars);
    exit(EXIT_SUCCESS);
}

/**
 * Return the number of elements in the queue.
 */
size_t queue_size(QueueNode *head) {
    size_t size = 0;
    QueueNode *current = head;
    while (current != NULL) {
        size++;
        current = current->next;
    }
    return size;
}

/**
 * Adds a new node to the queue right after the given node.
 */
void queue_add(QueueNode *after, char floor[MAX_FLOOR_LENGTH], char direction) {
    // Do not add the same floor+direction twice
    if (after->next != NULL && strncmp(after->next->floor, floor, MAX_FLOOR_LENGTH) == 0 && after->next->direction == direction) {
        return;
    }
    
    QueueNode *new_node = malloc(sizeof(QueueNode));
    if (new_node == NULL) {
        perror("malloc()");
        exit(EXIT_FAILURE);
    }

    strncpy(new_node->floor, floor, MAX_FLOOR_LENGTH);
    new_node->direction = direction;
    new_node->next = after->next;
    after->next = new_node;
}

/*
* Pushes a new node to the front of the queue.
*/
void queue_push_front(QueueNode **head, char floor[MAX_FLOOR_LENGTH], char direction) {
    QueueNode *new_node = malloc(sizeof(QueueNode));
    if (new_node == NULL) {
        perror("malloc()");
        exit(EXIT_FAILURE);
    }

    strncpy(new_node->floor, floor, MAX_FLOOR_LENGTH);
    new_node->direction = direction;
    new_node->next = *head;
    *head = new_node;
}

/*
* Removes the first node from the queue.
*/
void queue_pop(QueueNode **head) {
    if (*head == NULL) {
        return;
    }
    QueueNode *temp = *head;
    *head = (*head)->next;
    free(temp);
}

/*
* Removes the first node from the queue if the floor matches.
*/
void queue_pop_single(QueueNode **head, char *floor) {
    if (*head == NULL) {
        return;
    }
    if (strncmp((*head)->floor, floor, MAX_FLOOR_LENGTH) == 0) {
        queue_pop(head);
    }
}

/*
* Removes the first two nodes from the queue if the floors match.
* The need for removing two nodes is because the same floor can be added twice with different directions.
*/
void queue_pop_double(QueueNode **head, char *floor) {
    queue_pop_single(head, floor);
    queue_pop_single(head, floor);
}

/**
 * Adds a current floor at the front of the queue.
 * Returns 1 if the node was added, 0 otherwise (car is 'BETWEEN' and current floor is already at the front of the queue).
 */
int add_virtual_node(Car *car, char direction) {
    // If the car's status is Between we consider the current floor to be the next floor the elevator would go to
    if (strncmp(car->status, "Between", MAX_STATUS_LENGTH) == 0) {
        direction = are_consecutive_floors(car->current_floor, car->destination_floor) ? UP : DOWN;

        char next_floor[MAX_FLOOR_LENGTH];
        strncpy(next_floor, car->current_floor, MAX_FLOOR_LENGTH);
        // Increment or decrement the floor based on the direction
        set_next_floor(next_floor, direction);
        // If the next floor is the destination floor, we don't need to add it to the queue
        if (strncmp(next_floor, car->destination_floor, MAX_FLOOR_LENGTH) == 0) {
            return 0;
        }

        queue_push_front(&car->queue, next_floor, direction);
        return 1;
    }
    // If the car's status is anything else, the current floor is current floor...
    // Now we need to determine the direction
    // If the queue is empty, set the direction to the direction being requested by the call (in parameter)
    if (car->queue != NULL) {
        // If the first real entry in the queue is the same floor as the current floor, just take that entry's direction
        if (strncmp(car->current_floor, car->queue->floor, MAX_FLOOR_LENGTH) == 0) {
            direction = car->queue->direction;
        }
        // Otherwise, base the direction by looking whether the car would have to go up or down to get to the first real entry in the queue
        else {
            direction = are_consecutive_floors(car->current_floor, car->queue->floor) ? UP : DOWN;
        }
    }

    queue_push_front(&car->queue, car->current_floor, direction);
    return 1;
}

/**
 * Checks if the order of the source and destination floors is valid in respect to the given direction.
 * Returns 1 if the order is valid, 0 otherwise. If the floors are the same, the order is always valid.
 */
int is_valid_order(char *source_floor, char *destination_floor, char direction) {
    // Same floor -> valid order
    if (strncmp(source_floor, destination_floor, MAX_FLOOR_LENGTH) == 0) {
        return 1;
    }
    if (direction == UP && are_consecutive_floors(source_floor, destination_floor)) {
        return 1;
    }
    if (direction == DOWN && are_consecutive_floors(destination_floor, source_floor)) {
        return 1;
    }
    return 0;
}

void schedule_floors(Car * car, char *source_floor, char *destination_floor) {
    char direction = are_consecutive_floors(source_floor, destination_floor) ? UP : DOWN;

    int virtual_added = add_virtual_node(car, direction);
    // Find the suitable position to insert the source and destination floors
    QueueNode *current = car->queue ? car->queue->next : NULL;
    QueueNode *prev = car->queue;
    QueueNode *suitable_pos = NULL;

    // A special case is when the from floor is equal to the current floor (the virtual first item in the queue) and in the same direction.
    // If the status is Closing - it's too late, so the from and to floors will need to be inserted into the 3rd block.
    if (strncmp(car->queue->floor, source_floor, MAX_FLOOR_LENGTH) == 0
        && car->queue->direction == direction
        && strncmp(car->status, "Closing", MAX_STATUS_LENGTH) == 0) {
        prev = current;
        current = current->next;
    }

    while (current != NULL) {
        // We moved to another block -> reset previously found suitable position
        if (prev->direction != current->direction) {
            suitable_pos = NULL;
        }

        // We are in the block with different direction -> there is no suitable position
        if (prev->direction == current->direction && prev->direction != direction) {
            prev = current;
            current = current->next;
            continue;
        }

        if ((prev->direction != direction || is_valid_order(prev->floor, source_floor, direction))
        && (current->direction != direction || is_valid_order(source_floor, current->floor, direction))) {
            suitable_pos = prev;
        }
        if (suitable_pos != NULL
        && (prev->direction != direction || is_valid_order(prev->floor, destination_floor, direction))
        && (current->direction != direction || is_valid_order(destination_floor, current->floor, direction))) {
            break;
        }

        prev = current;
        current = current->next;
    }
    // No suitable position found -> add to the end
    if (!suitable_pos) {
        queue_add(prev, source_floor, direction);
        queue_add(prev->next, destination_floor, direction);
    }
    // Suitable position found -> add at the suitable position
    else {
        queue_add(suitable_pos, source_floor, direction);
        prev = (suitable_pos == prev) ? prev->next : prev;
        queue_add(prev, destination_floor, direction);
    }
    // Remove the virtual node if it was added
    if (virtual_added) {
        queue_pop(&car->queue);
    }
}

/**
 * Returns the car that is the most suitable for the call.
 * Most suitable car is the least busy one - the one with the least entries in the queue.
 * If no car is suitable, returns NULL.
 */
Car * choose_car(char *source_floor, char *destination_floor) {
    Car * car = NULL;
    size_t min_entries = SIZE_MAX;

    for (size_t i = 0; i < cv_size(&cars); i++) {
        Car *current_car = cv_get_at(&cars, i);
        // Check if the car can go to the source and destination floors
        if (!is_floor_within_bounds(source_floor, current_car->lowest_floor, current_car->highest_floor)
            || !is_floor_within_bounds(destination_floor, current_car->lowest_floor, current_car->highest_floor)) {
            continue;
        }
        size_t entries = queue_size(current_car->queue);
        // Less busy car found -> new ideal car
        if (entries < min_entries) {
            min_entries = entries;
            car = current_car;
        }
    }
    
    return car;
}

/**
 * Handles a TCP message from the call pad.
 * Attemps to schedule a car and returns the result to the call pad.
 */
void handle_call(int clientfd, char *source_floor, char *destination_floor) {
    // There are no cars connected
    if (cv_size(&cars) == 0) {
        send_message(clientfd, "UNAVAILABLE");
    }
    // Choose the car that is the most suitable for the call
    Car *car = choose_car(source_floor, destination_floor);
    // No car available for the call
    if (car == NULL) {
        send_message(clientfd, "UNAVAILABLE");
        return;
    }

    pthread_mutex_lock(&car->mutex);

    schedule_floors(car, source_floor, destination_floor);
    // The car's destination floor differs from the first floor in the queue
    // or the car's current floor is equal to the first floor in the queue
    // -> message the car
    if (strncmp(car->destination_floor, car->queue->floor, MAX_FLOOR_LENGTH) != 0
        || strncmp(car->current_floor, car->queue->floor, MAX_FLOOR_LENGTH) == 0) {
        char msg[10] = {0};
        snprintf(msg, sizeof(msg), "FLOOR %s", car->queue->floor);
        send_message(car->clientfd, msg);
    }
    pthread_mutex_unlock(&car->mutex);

    // Send the name of the car that was dispatched: CAR {car_name}
    char msg[MAX_CAR_NAME_LENGTH + 5] = {0};
    snprintf(msg, sizeof(msg), "CAR %s", car->car_name);
    send_message(clientfd, msg);
}

/**
 * Updates the car's state based on the received status, current floor and destination floor.
 * If the car has arrived at the destination floor, the floor is removed from the queue.
 * If there are more floors in the queue, the next floor is scheduled.
 */
void update_car_state(Car *car, char *status, char *current_floor, char *destination_floor) {
    strncpy(car->status, status, MAX_STATUS_LENGTH);
    strncpy(car->current_floor, current_floor, MAX_FLOOR_LENGTH);
    strncpy(car->destination_floor, destination_floor, MAX_FLOOR_LENGTH);

    // The car did not arrive at the destination floor yet -> no further action required
    if (strncmp(status, "Opening", MAX_STATUS_LENGTH) != 0 || strncmp(current_floor, destination_floor, MAX_FLOOR_LENGTH) != 0) {
        return;
    }
    pthread_mutex_lock(&car->mutex);
    // Remove the current/destination floor from the queue
    queue_pop_double(&car->queue, current_floor);
    // Schedule the next floor if there is one
    if (car->queue != NULL) {
        char msg[10] = {0};
        snprintf(msg, sizeof(msg), "FLOOR %s", car->queue->floor);
        send_message(car->clientfd, msg);
    }
    pthread_mutex_unlock(&car->mutex);
}

/**
 * Maintains a connection with a car and manages its state.
 */
void manage_car(int clientfd, char *car_name, char *lowest_floor, char *highest_floor) {
    // Validate the floor numbers
    if (!is_valid_floor(lowest_floor) || !is_valid_floor(highest_floor) || !are_consecutive_floors(lowest_floor, highest_floor)) {
        send_message(clientfd, "INVALID");
        return;
    }

    // Initialize the car struct
    Car * car = malloc(sizeof(Car));
    strncpy(car->car_name, car_name, MAX_CAR_NAME_LENGTH);
    strncpy(car->lowest_floor, lowest_floor, MAX_FLOOR_LENGTH);
    strncpy(car->highest_floor, highest_floor, MAX_FLOOR_LENGTH);
    strncpy(car->status, "Closed", MAX_STATUS_LENGTH);
    strncpy(car->current_floor, lowest_floor, MAX_FLOOR_LENGTH);
    car->clientfd = clientfd;
    car->queue = NULL;
    pthread_mutex_init(&car->mutex, NULL);
    // Insert the car into the cars vector
    cv_push(&cars, car);

    char *tokens[4];
    // Loop to receive messages from the car and take appropriate action
    while (1) {
        char *msg = receive_msg(car->clientfd);
        // Car is gonna disconnect -> free memory and return
        if (msg == NULL || strcmp(msg, "INDIVIDUAL SERVICE") == 0 || strcmp(msg, "EMERGENCY") == 0) {
            free(msg);
            cv_remove(&cars, car);
            pthread_mutex_destroy(&car->mutex);
            free(car);
            return;
        }
        tokenize_message(msg, tokens, 4);

        if (strncmp(tokens[0], "STATUS", MAX_STATUS_LENGTH) == 0) {
            update_car_state(car, tokens[1], tokens[2], tokens[3]);
        }
        free(msg);
    }
}

/**
 * Handles a client connection and branches off to the appropriate handler based on the message received.
 */
void * handle_client(void *arg) {
    int clientfd = *((int *) arg);
    free(arg);

    char *msg = receive_msg(clientfd);
    if (msg == NULL) {
        if (shutdown(clientfd, SHUT_RDWR) == -1) {
            perror("shutdown()");
        }
        if (close(clientfd) == -1) {
            perror("close()");
        }
        pthread_exit(NULL);
    }

    char *tokens[4];
    tokenize_message(msg, tokens, 4);

    if (strncmp(tokens[0], "CALL", 4) == 0) {
        handle_call(clientfd, tokens[1], tokens[2]);
    }
    else if (strncmp(tokens[0], "CAR", 3) == 0) {
        manage_car(clientfd, tokens[1], tokens[2], tokens[3]);
    }
    else {
        send_message(clientfd, "INVALID");
    }
    
    free(msg);

    if (shutdown(clientfd, SHUT_RDWR) == -1) {
        perror("shutdown()");
    }
    if (close(clientfd) == -1) {
        perror("close()");
    }

    pthread_exit(NULL);
}

int main(void) {
    listensockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listensockfd == -1) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    // Don't terminate the program when writing to a closed socket
    signal(SIGPIPE, SIG_IGN);
    // Register signal handler for SIGINT (Ctrl + C)
    signal(SIGINT, handle_sigint);

    int opt_enable = 1;
    if (setsockopt(listensockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1) {
        perror("setsockopt()");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(listensockfd, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror("bind()");
        exit(EXIT_FAILURE);
    }

    if (listen(listensockfd, MAX_CLIENTS) == -1) {
        perror("listen()");
        exit(EXIT_FAILURE);
    }

    cv_init(&cars);

    while (1) {
        int *clientfd = malloc(sizeof(*clientfd));
        if (clientfd == NULL) {
            perror("malloc()");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in clientaddr;
        socklen_t clientaddr_len = sizeof(clientaddr);

        *clientfd = accept(listensockfd, (struct sockaddr *) &clientaddr, &clientaddr_len);
        if (*clientfd == -1) {
            perror("accept()");
            continue;
        }

        pthread_t thread_id;
        int thread_create_result = pthread_create(&thread_id, NULL, handle_client, (void *) clientfd);
        if (thread_create_result != 0) {
            fprintf(stderr, "pthread_create() failed: %s\n", strerror(thread_create_result));
            if (close(*clientfd) == -1) {
                perror("close()");
            }
            continue;
        }

        pthread_detach(thread_id);
    }

    cv_destroy(&cars);
    close(listensockfd);
}
