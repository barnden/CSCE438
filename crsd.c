#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <string>

#include "interface.h"

/*
 * Create a server using given port
 *
 * @parameter port    port given by command line argument
 * 
 * @return socket file descriptor
 */
int establish_server(std::string port)
{
    auto hints = addrinfo {};

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto* result = std::add_pointer_t<addrinfo> {};

    if (getaddrinfo(NULL, port.c_str(), &hints, &result) < 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }

    // Attempt to create socket
    auto socketfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

    if (socketfd < 0) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    // Attempt to bind to port so we can listen to client connections
    if (bind(socketfd, result->ai_addr, result->ai_addrlen) < 0) {
        close(socketfd);
        perror("bind()");
        exit(EXIT_FAILURE);
    }

    // Disable Nagle's algorithm
    // TODO: See if this is actually something we want to do on this specific socket
    auto unused = 1;
    setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY, &unused, sizeof(decltype(TCP_NODELAY)));

    // Result struct no longer needed
    freeaddrinfo(result);

    // Listen to as many clients as possible
    // On most systems this is capped to 4096
    // This can be checked by running: cat /proc/sys/net/core/somaxconn
    if (listen(socketfd, -1) < 0) {
        perror("listen()");
        exit(EXIT_FAILURE);
    }

    return socketfd;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: enter port number\n";
        return EXIT_FAILURE;
    }

    auto socket = establish_server(std::string { argv[1] });

    return EXIT_SUCCESS;
}
