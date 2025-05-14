#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <netdb.h>
#include <sys/socket.h>

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
}
