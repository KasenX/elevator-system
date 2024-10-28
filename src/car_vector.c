#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include "car_vector.h"

void cv_init( car_vector_t *vec ) {
    vec->capacity = CV_INITIAL_CAPACITY;
    vec->size = 0;
    vec->data = malloc(vec->capacity * sizeof(Car *));
    pthread_mutex_init(&vec->mutex, NULL);
}

static void cv_ensure_capacity( car_vector_t *vec, size_t new_size ) {
    if (new_size <= vec->capacity) {
        return;
    }
    int new_capacity = fmax(vec->capacity * CV_GROWTH_FACTOR, new_size);
    vec->capacity = new_capacity;
    vec->data = realloc(vec->data, new_capacity * sizeof(Car *));
}

void cv_destroy( car_vector_t *vec ) {
    pthread_mutex_lock(&vec->mutex);

    vec->capacity = 0;
    vec->size = 0;
    
    for (size_t i = 0; i < vec->size; i++) {
        free(vec->data[i]);
    }

    free(vec->data);
    vec->data = NULL;

    pthread_mutex_unlock(&vec->mutex);
    pthread_mutex_destroy(&vec->mutex);
}

size_t cv_size( car_vector_t *vec ) {
    pthread_mutex_lock(&vec->mutex);
    size_t size = vec->size;
    pthread_mutex_unlock(&vec->mutex);
    return size;
}

void cv_push( car_vector_t *vec, Car * new_item ) {
    pthread_mutex_lock(&vec->mutex);

    cv_ensure_capacity(vec, vec->size + 1);
    vec->data[vec->size] = new_item;
    vec->size++;

    pthread_mutex_unlock(&vec->mutex);
}

Car * cv_get_at( car_vector_t *vec, size_t index) {
    pthread_mutex_lock(&vec->mutex);

    Car * item = NULL;
    if (index < vec->size) {
        item = vec->data[index];
    }

    pthread_mutex_unlock(&vec->mutex);

    return item;
}

static void cv_remove_at( car_vector_t *vec, size_t pos ) {
    if (pos >= vec->size) {
        return;
    }
    for (size_t i = pos; i < vec->size - 1; i++) {
        vec->data[i] = vec->data[i + 1];
    }
    vec->size--;
}

void cv_remove( car_vector_t *vec, Car * item ) {
    pthread_mutex_lock(&vec->mutex);

    for (size_t i = 0; i < vec->size; i++) {
        if (vec->data[i] == item) {
            cv_remove_at(vec, i);
            break;
        }
    }

    pthread_mutex_unlock(&vec->mutex);
}
