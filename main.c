#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/socket.h>

#define STATUS_MASK (uint16_t)(1 << 15)
#define TEMP_MASK (uint16_t)(0xffff >> 1)

#define NETWORK_POWER (uint16_t)(0 << 15)
#define BATTERY_POWER (uint16_t)(1 << 15)

#define COMBINE_TEMP_STATUS(temp, status) (uint16_t)( ( (temp) & TEMP_MASK ) | (status) )

// this is the packed size of reading
#define READING_SIZE (4 + 2 + 1 + 1)

struct reading {
    // note that this is subject to the year 2038 problem.
    // change to uint64_t when needed.
    int32_t timestamp;

    // this 16-bit value stores both the temperature and status.
    // the status and temperature are obtained by (temp_status & STATUS_MASK), and (temp_status & TEMP_MASK).
    //
    // the temperature stored is 10x the true temperature, so that it can be stored as a whole number
    // note that this can support a wider range of temperatures than in the specification
    uint16_t temp_status;

    // id incremented with each reading
    uint8_t id;

    // the checksum is computed as the negation of the sum of all the bytes of the structure.
    // to validate the message, the sum of all bytes (including the checksum) must be 0
    uint8_t checksum;
};

// computes the checksum for the reading.
void reading_compute_checksum(struct reading *packet) {
    packet->checksum = 0;
    for (int i = 0; i < sizeof(*packet) - 1; ++i)
        packet->checksum += ((uint8_t*)packet)[i];
    packet->checksum = -packet->checksum;
}

// validates the correctness of the reading.
// the reading is valid if and only if this function returns 0.
char reading_validate(struct reading *packet) {
    uint8_t sum = 0;
    for (int i = 0; i < sizeof(*packet); ++i)
        sum += ((uint8_t*)packet)[i];
    return (char)sum;
}

// initializes the reading packet.
// the user should use this function instead of manually initializing the struct,
// due to the cap on temperature and checksum.
// the temperature argument is clamped to the range 200-1200 and is then divided by 10 to get the true
// temperature.
// status should be one of NETWORK_POWER, BATTERY_POWER.
void reading_initialize(struct reading *packet, time_t timestamp, unsigned short temperature,
        short status, uint8_t id) {
    // clamp the temperature
    if (temperature < 200) temperature = 200;
    if (temperature > 1200) temperature = 1200;

    packet->timestamp = timestamp;
    packet->temp_status = COMBINE_TEMP_STATUS(temperature, status);
    packet->id = id;
    reading_compute_checksum(packet);
}

// print the given reading in a user-friendly way.
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

// this function serializes the packet and converts each field to network byte order
// before sending it through the given socket to the given destination.
// we serialize the packet manually, because packed structs are platform dependent.
// returns the status code of sendto.
int reading_sendto(struct reading *packet, int sockfd, struct sockaddr *address, socklen_t addrlen) {
    uint8_t buffer[READING_SIZE];
    uint32_t timestamp_no = htonl(packet->timestamp);
    uint16_t temp_status_no = htons(packet->temp_status);

    memcpy(buffer, &timestamp_no, 4);
    memcpy(buffer + 4, &temp_status_no, 2);
    memcpy(buffer + 6, &packet->id, 1);
    memcpy(buffer + 7, &packet->checksum, 1);

    return sendto(sockfd, buffer, READING_SIZE, 0, address, addrlen);
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

    char *ipaddress = argv[1];
    char *port = argv[2];

    char *endptr;
    unsigned int wait_interval = strtoul(argv[3], &endptr, 10);
    if (*endptr != 0) { // check if the wait time is a valid integer
        fprintf(stderr, "invalid wait value\n");
        fprintf(stderr, "usage: udptempbot ipaddress port wait\n");
        return 1;
    }
    
    // get destination info
    struct sockaddr_storage address;
    socklen_t addrlen;
    int status;
    if ((status = fetch_destination(ipaddress, port, &address, &addrlen)) != 0) {
        fprintf(stderr, "error while fetching address: %s\n", gai_strerror(status));
        return 1;
    }
    
    // prepare the socket
    int sockfd = get_udp_socket(address.ss_family);
    if (sockfd == -1) {
        perror("socket failed");
        return 1;
    }
    
    // run the bot
    uint8_t id = 0;
    while (1) {
        // generate random reading
        struct reading current_reading;
        reading_initialize(&current_reading, time(NULL), 200 + rand() % 1001,
                (rand() % 2) ? NETWORK_POWER : BATTERY_POWER, id);

        // send the reading
        int status = reading_sendto(&current_reading, sockfd, (struct sockaddr *)&address, addrlen);
        if (status == -1) {
            perror("sendto failed");
            return 1;
        }
        
        // display it to the user
        reading_display(&current_reading);
        putc('\n', stdout);

        sleep(wait_interval);
        id++;
    }

    // close socket
    shutdown(sockfd, SHUT_RDWR);
}
