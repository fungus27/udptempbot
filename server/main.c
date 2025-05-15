#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define STATUS_MASK (uint16_t)(1 << 15)
#define TEMP_MASK (uint16_t)(0xffff >> 1)

#define NETWORK_POWER (uint16_t)(0 << 15)
#define BATTERY_POWER (uint16_t)(1 << 15)

#define READING_SIZE (4 + 2 + 1 + 1)

struct reading {
    int32_t timestamp;
    uint16_t temp_status;
    uint8_t id;
    uint8_t checksum;
};

char reading_validate(struct reading *packet) {
    uint8_t sum = 0;
    for (int i = 0; i < sizeof(*packet); ++i)
        sum += ((uint8_t*)packet)[i];
    return (char)sum;
}

void reading_display(struct reading *packet) {
    const char *status = (packet->temp_status & STATUS_MASK) == NETWORK_POWER ? "network" : "battery";
    const char *validity = reading_validate(packet) == 0 ? "valid" : "invalid";

    // calculate the whole and decimal part of the temperature
    unsigned short whole = (packet->temp_status & TEMP_MASK) / 10;
    unsigned short decimal = (packet->temp_status & TEMP_MASK) % 10;

    printf( "ID: %hhu\n"
            "Timestamp: %d\n"
            "Temperature: %hu.%hu\n"
            "Power status: %s\n"
            "Checksum: 0x%hhx (%s)\n",
            packet->id, packet->timestamp, whole, decimal, status, packet->checksum, validity);
}

int reading_recvfrom(struct reading *packet, int sockfd, struct sockaddr_storage *address, socklen_t *addrlen) {
    uint8_t buffer[READING_SIZE];
    int status = recvfrom(sockfd, buffer, READING_SIZE, 0, (struct sockaddr*)address, addrlen);
    if (status == -1)
        return -1;

    memcpy(&packet->timestamp, buffer, 4);
    memcpy(&packet->temp_status, buffer + 4, 2);
    memcpy(&packet->id, buffer + 6, 1);
    memcpy(&packet->checksum, buffer + 7, 1);

    packet->timestamp = ntohl(packet->timestamp);
    packet->temp_status = ntohs(packet->temp_status);

    return status;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: udpserver port\n");
        return 1;
    }

    char *port = argv[1];

    int sockfd = socket(AF_INET, SOCK_DGRAM, getprotobyname("udp")->p_proto);

    struct addrinfo hints, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    int status;
    if ((status = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "error while setting up port: %s\n", gai_strerror(status));
        return 1;
    }
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        return 1;
    }
    freeaddrinfo(res);

    printf("Listening on port %s...\n", port);
    
    while (1) {
        struct reading r;
        struct sockaddr_storage address;
        socklen_t addrlen = sizeof(address);
        char str_address[INET6_ADDRSTRLEN];
        in_port_t address_port;

        if (reading_recvfrom(&r, sockfd, &address, &addrlen) == -1) {
            perror("recvfrom");
            return 1;
        }

        // convert the incoming address to a string
        if (address.ss_family == AF_INET) {
            struct sockaddr_in ipv4 = *((struct sockaddr_in*)&address);
            if (!inet_ntop(AF_INET, &(ipv4.sin_addr), str_address, INET6_ADDRSTRLEN)) {
                perror("inet_ntop");
                return 1;
            }
            address_port = ipv4.sin_port;
        }
        else if (address.ss_family == AF_INET6) {
            struct sockaddr_in6 ipv6 = *((struct sockaddr_in6*)&address);
            if (!inet_ntop(AF_INET6, &(ipv6.sin6_addr), str_address, INET6_ADDRSTRLEN)) {
                perror("inet_ntop");
                return 1;
            }
            address_port = ipv6.sin6_port;
        }
        
        printf("Reading from %s:%hu\n", str_address, address_port);
        reading_display(&r);
        putc('\n', stdout);
    }

    shutdown(sockfd, SHUT_RDWR);
}
