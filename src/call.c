#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "shared.h"

int get_car_name(const char *response, const char **car_name) {
    const char *prefix = "CAR ";
    size_t prefix_len = strlen(prefix);

    // Response does not include car name
    if (strncmp(response, prefix, prefix_len) != 0) {
        return 0;
    }

    *car_name = response + prefix_len;

    return 1;
}

int main(int argc, char **argv) {
    // Check if exactly 2 arguments are passed
    if (argc != 3) {
        printf("Usage: %s {source floor} {destination floor}\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Check if the floor numbers are valid
    if (!is_valid_floor(argv[1]) || !is_valid_floor(argv[2])) {
        printf("Invalid floor(s) specified.\n");
        exit(EXIT_FAILURE);
    }
    // Check if the floor numbers differ
    if (strcmp(argv[1], argv[2]) == 0) {
        printf("You are already on that floor!\n");
        exit(EXIT_FAILURE);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    const char *ipaddress = "127.0.0.1";
    if (inet_pton(AF_INET, ipaddress, &addr.sin_addr) != 1) {
        fprintf(stderr, "inet_pton(%s)\n", ipaddress);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Unable to connect to elevator system.\n");
        exit(EXIT_FAILURE);
    }

    char msg[13] = {0};
    snprintf(msg, sizeof(msg), "CALL %s %s", argv[1], argv[2]);
    
    if (send_message(sockfd, msg) == -1) {
        fprintf(stderr, "Failed to send request to elevator system.\n");
        exit(EXIT_FAILURE);
    }

    char *response = receive_msg(sockfd);

    if (response == NULL) {
        fprintf(stderr, "Failed to receive response from elevator system.\n");
        exit(EXIT_FAILURE);
    }

    const char *car_name = NULL;
    if (get_car_name(response, &car_name)) {
        printf("Car %s is arriving.\n", car_name);
    } else {
        printf("Sorry, no car is available to take this request.\n");
    }
    free(response);

    if (shutdown(sockfd, SHUT_RDWR) == -1) {
        perror("shutdown()");
        exit(EXIT_FAILURE);
    }
    if (close(sockfd) == -1) {
        perror("close()");
        exit(EXIT_FAILURE);
    }
}
