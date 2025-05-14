#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <netdb.h>
#include <sys/socket.h>

#define STATUS_MASK (uint16_t)(1 << 16)
#define TEMP_MASK (uint16_t)(0xffff >> 1)

#define NETWORK_POWER (uint16_t)(0 << 16)
#define BATTERY_POWER (uint16_t)(1 << 16)

#define COMBINE_TEMP_STATUS(temp, status) (uint16_t)( ( (temp) & TEMP_MASK ) | (status) )

struct reading {
    // note that this is subject to the year 2038 problem.
    // change to uint64_t when needed.
    int32_t timestamp;

    // this 16-bit value stores both the temperature and status.
    // the status and temperature are obtained by (temp_status & STATUS_MASK), and (temp_status & TEMP_MASK).
    //
    // the true temperature is calculated from the formula 20.0 + (temperature_status & TEMP_MASK)/10.0.
    // note that it has a bigger range than in the specification
    uint16_t temp_status;

    // id incremented with each reading
    uint8_t id;

    // the checksum is computed as the negation of the sum of all the bytes of the structure.
    // to validate the message, the sum of all bytes (including the checksum) must be 0
    int8_t checksum;
};

// computes the checksum for the reading.
void reading_compute_checksum(struct reading *packet) {
    packet->checksum = 0;
    for (int i = 0; i < sizeof(*packet) - 1; ++i)
        packet->checksum += ((int8_t*)packet)[i];
    packet->checksum = -packet->checksum;
}

// validates the correctness of the reading.
// the reading is valid if and only if this function returns 0.
char reading_validate(struct reading *packet) {
    int8_t sum = 0;
    for (int i = 0; i < sizeof(*packet); ++i)
        sum += ((int8_t*)packet)[i];
    return (char)sum;
}

// print the given reading in a user-friendly way.
void reading_display(struct reading *packet) {
    const char *status = (packet->temp_status & STATUS_MASK) == NETWORK_POWER ? "network" : "battery";
    const char *validity = reading_validate(packet) == 0 ? "valid" : "invalid";

    // calculate the whole and decimal part of the temperature
    unsigned short whole = 20 + (packet->temp_status & TEMP_MASK) / 10;
    unsigned short decimal = (packet->temp_status & TEMP_MASK) % 10;

    printf( "ID: %hhu\n"
            "Timestamp: %d\n"
            "Temperature: %hu.%hu\n"
            "Power status: %s\n"
            "Checksum: 0x%hhx (%s)\n",
            packet->id, packet->timestamp, whole, decimal, status, packet->checksum, validity);
}

// returns an UDP socket descriptor for the specified ip address type or -1 on error (check errno)
int get_udp_socket(int domain) {
    return socket(domain, SOCK_DGRAM, getprotobyname("udp")->p_proto);
}

// gets the address info of the UDP host specified by the address-port pair and stores it in res.
// *addrlen will contain the length of the address.
// returns the getaddressinfo status code.
int fetch_destination(const char *address, const char *port, struct sockaddr_storage *res, socklen_t *addrlen) {
    // prepare structures for fetching destination address
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // allow both ipv4 and ipv6
    hints.ai_socktype = SOCK_DGRAM; // use UDP

    // fetch destination address
    // note that getaddrinfo is guaranteed to return at least one result (if it doesn't error)
    int status;
    if ((status = getaddrinfo(address, port, &hints, &result)) != 0)
        return status;
    
    // extract the address
    // sockaddr_storage is guaranteed to be big enough to hold any address
    memcpy(res, result->ai_addr, result->ai_addrlen);
    *addrlen = result->ai_addrlen;

    freeaddrinfo(result);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "usage: udptempbot ipaddress port wait\n");
        return 1;
    }
    
    // get destination info
    struct sockaddr_storage address;
    socklen_t addrlen;
    int status;
    if ((status = fetch_destination(argv[1], argv[2], &address, &addrlen)) != 0) {
        fprintf(stderr, "error while fetching address: %s\n", gai_strerror(status));
        return 1;
    }
    
    // prepare the socket
    int sockfd = get_udp_socket(address.ss_family);
    if (sockfd == -1) {
        perror("socket failed");
        return 1;
    }
    
    // send test message
    const char *message = "Witam, witam.";
    int sent = sendto(sockfd, message, strlen(message) + 1, 0,
            (struct sockaddr*)&address, addrlen);
    if (sent == -1) {
        perror("sendto failed");
        return 1;
    }

    // close socket
    shutdown(sockfd, SHUT_RDWR);

    struct reading r;
    r.timestamp = time(NULL);
    r.temp_status = COMBINE_TEMP_STATUS(405, NETWORK_POWER);
    r.id = 4;
    reading_compute_checksum(&r);
    reading_display(&r);
}
