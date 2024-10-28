#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include "shared.h"

int recv_looped(int fd, void *buf, size_t sz)
{
    char *ptr = buf;
    size_t remain = sz;

    while (remain > 0) {
        ssize_t received = read(fd, ptr, remain);
        if (received == -1) {
            return -1;
        }
        ptr += received;
        remain -= received;
    }
    return 0;
}

char *receive_msg(int fd)
{
    uint32_t nlen;
    if (recv_looped(fd, &nlen, sizeof(nlen)) == - 1) {
        return NULL;
    }
    uint32_t len = ntohl(nlen);
    
    char *buf = malloc(len + 1);
    buf[len] = '\0';
    if (recv_looped(fd, buf, len) == -1) {
        free(buf);
        return NULL;
    }
    return buf;
}

int send_looped(int fd, const void *msg, size_t sz)
{
    const char *ptr = msg;
    size_t remain = sz;

    while (remain > 0) {
        ssize_t sent = write(fd, ptr, remain);
        if (sent == -1) {
            return -1;
        }
        ptr += sent;
        remain -= sent;
    }

    return 0;
}

int send_message(int fd, const char *msg)
{
    uint32_t len = htonl(strlen(msg));

    if (send_looped(fd, &len, sizeof(len)) == -1) {
        return -1;
    }

    if (send_looped(fd, msg, strlen(msg)) == -1) {
        return -1;
    }

    return 0;
}

int is_valid_floor(const char *floor) {
    int len = strlen(floor);

    // Check for format: B##
    if (floor[0] == 'B') {
        // Floor number can't start with 0
        if (floor[1] == '0') {
            return 0;
        }
        // Ensure it's 'B' followed by 1 to 2 digits
        if (len < 2 || len > 3) {
            return 0;
        }
        for (int i = 1; i < len; i++) {
            if (!isdigit(floor[i])) {
                return 0;
            }
        }
    }
    // Check for format: ###
    else {
        // Floor number can't start with 0
        if (floor[0] == '0') {
            return 0;
        }
        // Ensure it's 1 to 3 digits
        if (len < 1 || len > 3) {
            return 0;
        }
        for (int i = 0; i < len; i++) {
            if (!isdigit(floor[i])) {
                return 0;
            }
        }
    }
    return 1;
}

int are_consecutive_floors(const char *before, const char *after) {
    if (before[0] == 'B' && after[0] == 'B') {
        return atoi(before + 1) >= atoi(after + 1);
    } else if (before[0] == 'B') {
        return 1;
    } else if (after[0] == 'B') {
        return 0;
    } else {
        return atoi(before) <= atoi(after);
    }
}

int is_floor_within_bounds(const char *floor, const char *lowest_floor, const char *highest_floor) {
    return are_consecutive_floors(lowest_floor, floor) && are_consecutive_floors(floor, highest_floor);
}

void tokenize_message(char *msg, char *tokens[], int max_tokens) {
    int count = 0;
    char *token = strtok(msg, " ");

    while (token != NULL && count < max_tokens) {
        tokens[count++] = token;
        token = strtok(NULL, " ");
    }

    // Fill remaining tokens with NULL for safety
    for (int i = count; i < max_tokens; i++) {
        tokens[i] = NULL;
    }
}

void increment_floor(char *floor) {
    if (floor[0] == 'B') {
        int floor_num = atoi(floor + 1);
        // Move to normal floor
        if (floor_num == 1) {
            strcpy(floor, "1");
        }
        // Increment basement floor
        else {
            snprintf(floor, 4, "B%d", floor_num - 1);
        }
    } else {
        int floor_num = atoi(floor);
        // Max limit, return "999"
        if (floor_num == 999) {
            strcpy(floor, "999");
        }
        // Increment normal floor
        else {
            snprintf(floor, 4, "%d", floor_num + 1);
        }
    }
}

void decrement_floor(char *floor) {
    if (floor[0] == 'B') {
        int floor_num = atoi(floor + 1);
        // Max limit, return "B99"
        if (floor_num == 99) {
            strcpy(floor, "B99");
        }
        // Decrement basement floor
        else {
            snprintf(floor, 4, "B%d", floor_num + 1);
        }
    } else {
        int floor_num = atoi(floor);
        // Move to basement floor
        if (floor_num == 1) {
            strcpy(floor, "B1");
        }
        // Decrement normal floor
        else {
            snprintf(floor, 4, "%d", floor_num - 1);
        }
    }
}

void set_next_floor(char *floor, char direction) {
    if (direction == UP) {
        increment_floor(floor);
    } else {
        decrement_floor(floor);
    }
}
