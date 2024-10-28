#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "shared.h"

car_shared_mem* open_shared_memory(const char * share_name) {
    int fd = shm_open(share_name, O_RDWR, 0666);
    if (fd == -1) {
        return NULL;
    }

    car_shared_mem *shm = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap()");
        close(fd);
        return NULL;
    }

    close(fd);

    return shm;
}

int main(int argc, char **argv) {
    // Check if exactly 2 arguments are passed
    if (argc != 3) {
        printf("Usage: %s {car name} {operation}\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Calculate the length of the car name and ensure it doesn't exceed the limit
    size_t car_name_len = strlen(argv[1]);
    size_t prefix_len = strlen(SHM_NAME_PREFIX);

    if (car_name_len + prefix_len >= MAX_CAR_NAME_LENGTH) {
        printf("Car name too long.\n");
        exit(EXIT_FAILURE);
    }

    char share_name[MAX_CAR_NAME_LENGTH];
    (void) strncpy(share_name, SHM_NAME_PREFIX, prefix_len + 1);                // Copy "/car" (including null terminator)
    (void) strncat(share_name, argv[1], MAX_CAR_NAME_LENGTH - prefix_len - 1);  // Concatenate car name

    car_shared_mem *shm = open_shared_memory(share_name);

    if (shm == NULL) {
        printf("Unable to access car %s.\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[2], "open") == 0) {
        pthread_mutex_lock(&shm->mutex);
        shm->open_button = 1;
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
    }
    else if (strcmp(argv[2], "close") == 0) {
        pthread_mutex_lock(&shm->mutex);
        shm->close_button = 1;
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
    }
    else if (strcmp(argv[2], "stop") == 0) {
        pthread_mutex_lock(&shm->mutex);
        shm->emergency_stop = 1;
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
    }
    else if (strcmp(argv[2], "service_on") == 0) {
        pthread_mutex_lock(&shm->mutex);
        shm->individual_service_mode = 1;
        shm->emergency_mode = 0;
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
    }
    else if (strcmp(argv[2], "service_off") == 0) {
        pthread_mutex_lock(&shm->mutex);
        shm->individual_service_mode = 0;
        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
    }
    else if (strcmp(argv[2], "up") == 0) {
        pthread_mutex_lock(&shm->mutex);

        if (!shm->individual_service_mode) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation only allowed in service mode.\n");
            exit(EXIT_FAILURE);
        }
        if (strcmp(shm->status, "Between") == 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while elevator is moving.\n");
            exit(EXIT_FAILURE);
        }
        if (strcmp(shm->status, "Closed") != 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while doors are open.\n");
            exit(EXIT_FAILURE);
        }

        char floor[4];
        strcpy(floor, shm->current_floor);
        increment_floor(floor);
        strcpy(shm->destination_floor, floor);

        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
    }
    else if (strcmp(argv[2], "down") == 0) {
        pthread_mutex_lock(&shm->mutex);

        if (!shm->individual_service_mode) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation only allowed in service mode.\n");
            exit(EXIT_FAILURE);
        }
        if (strcmp(shm->status, "Between") == 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while elevator is moving.\n");
            exit(EXIT_FAILURE);
        }
        if (strcmp(shm->status, "Closed") != 0) {
            pthread_mutex_unlock(&shm->mutex);
            printf("Operation not allowed while doors are open.\n");
            exit(EXIT_FAILURE);
        }

        char floor[4];
        strcpy(floor, shm->current_floor);
        decrement_floor(floor);
        strcpy(shm->destination_floor, floor);

        pthread_cond_broadcast(&shm->cond);
        pthread_mutex_unlock(&shm->mutex);
    }
    else {
        printf("Invalid operation.\n");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
