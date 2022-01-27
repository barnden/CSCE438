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
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "interface.h"
#include "message.h"

/*
 * TODO: IMPLEMENT BELOW THREE FUNCTIONS
 */
int connect_to(const char* host, const int port);
struct Reply process_command(const int sockfd, char* command);
void process_chatmode(const char* host, const int port);

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: enter host address and port number\n");
        exit(1);
    }

    display_title();

    while (1) {
        int sockfd = connect_to(argv[1], atoi(argv[2]));

        char command[MAX_DATA];
        get_command(command, MAX_DATA);

        struct Reply reply = process_command(sockfd, command);
        display_reply(command, reply);

        touppercase(command, strlen(command) - 1);
        if (strncmp(command, "JOIN", 4) == 0) {
            printf("Now you are in the chatmode\n");
            process_chatmode(argv[1], reply.port);
        }

        close(sockfd);
    }

    return 0;
}

/*
 * Connect to the server using given host and port information
 *
 * @parameter host    host address given by command line argument
 * @parameter port    port given by command line argument
 *
 * @return socket fildescriptor
 */
int connect_to(const char* host, const int port)
{
    // Establish a TCP connection with the server
    // No need to memset hints to 0 because it's zero initialized in C++
    auto hints = addrinfo {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto* result = std::add_pointer_t<addrinfo> {};

    // Attempt to resolve host
    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &result) < 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }

    // Attempt to create a socket
    auto socketfd = -1;
    auto* rp = std::add_pointer_t<addrinfo> {};
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        socketfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (socketfd == -1)
            continue;

        if (connect(socketfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;

        close(socketfd);
    }

    // Result struct no longer needed
    freeaddrinfo(result);

    // No connection could be established if rp is NULL
    if (rp == NULL)
        exit(EXIT_FAILURE);

    // Disable Nagle's algorithm (see design document)
    auto unused = 1;
    setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY, &unused, sizeof(decltype(TCP_NODELAY)));

    return socketfd;
}

/*
 * Send an input command to the server and return the result
 *
 * @parameter sockfd   socket file descriptor to commnunicate
 *                     with the server
 * @parameter command  command will be sent to the server
 *
 * @return    Reply
 */
struct Reply process_command(const int sockfd, char* command)
{
    auto buffer = std::make_unique<char[]>(MAX_DATA);
    auto offset = 0;
    auto message = MessageType::INVALID;

    if (!strncmp(command, "CREATE", 6)) {
        message = CREATE;
        offset = 7;
    } else if (!strncmp(command, "DELETE", 6)) {
        message = DELETE;
        offset = 7;
    } else if (!strncmp(command, "JOIN", 4)) {
        message = JOIN;
        offset = 5;
    } else if (!strncmp(command, "LIST", 4)) {
        message = LIST;
        offset = 4;
    }

    memcpy(buffer.get(), &message, sizeof(message));

    // Offset is to ignore the command text and only pass the arguments to the server
    strcpy(buffer.get() + sizeof(message), command + offset);

    // Send the command to the server
    send(sockfd, buffer.get(), sizeof(message) + strlen(command) - offset + 1, 0);

    memset(buffer.get(), 0, MAX_DATA);

    auto bytes = recv(sockfd, buffer.get(), MAX_DATA, 0);

    if (bytes < 0) {
        perror("recv()");
        exit(EXIT_FAILURE);
    }

    auto reply = Reply {};
    auto cursor = buffer.get();

    // Verify that we have indeed received a RESPONSE message from the server
    if (reinterpret_cast<MessageType&>(*cursor) != MessageType::RESPONSE) {
        std::cerr << "expected response message type from server.\n";
        exit(EXIT_FAILURE);
    }

    cursor += sizeof(MessageType);

    reply.status = reinterpret_cast<Status&>(*cursor);

    cursor += sizeof(Status);

    auto data = uint32_t {};

    if (message == JOIN) {
        reply.port = *reinterpret_cast<decltype(reply.port)*>(cursor);
        cursor += sizeof(reply.port);

        reply.num_member = *reinterpret_cast<decltype(reply.num_member)*>(cursor);
        cursor += sizeof(reply.num_member);

        // std::cout << "PORT: " << reply.port << "\t NUMBER"
    } else if (message == LIST) {
        // TODO: Implement this
        // For the "LIST" command,
        // You are suppose to copy the list of chatroom to the list_room
        // variable. Each room name should be seperated by comma ','.
        // For example, if given command is "LIST", the Reply variable
        // will be set as following.
        //
        // Reply reply;
        // reply.status = SUCCESS;
        // strcpy(reply.list_room, list);
        //
        // "list" is a string that contains a list of chat rooms such
        // as "r1,r2,r3,"
    }

    return reply;
}

/*
 * Get into the chat mode
 *
 * @parameter host     host address
 * @parameter port     port
 */
void process_chatmode(const char* host, const int port)
{
    auto socketfd = connect_to(host, port);

    // We do not provide the user a method to return to command mode after entering chat mode

    // Threads are overkill but I don't remember how to use epoll()
    auto listener = std::thread([socketfd]() -> void {
        auto buffer = std::make_unique<char[]>(BUFSIZ);

        while (true) {
            auto bytes = recv(socketfd, buffer.get(), BUFSIZ, 0);

            if (bytes < 0) {
                perror("recv()");
                continue;
            }

            // If new message is shorter than previous message then we will start writing
            // the previous message without setting the sentinel value.
            buffer.get()[bytes] = '\0';

            // Write received message into STDOUT, append new line, and flush buffer
            std::cout << std::string { buffer.get() } << std::endl;
        }
    });

    listener.detach();

    while (true) {
        // Handle blocking I/O from fgets in main client thread
        auto buffer = std::make_unique<char[]>(BUFSIZ);
        get_message(buffer.get(), BUFSIZ);

        send(socketfd, buffer.get(), strlen(buffer.get()), 0);
    }
}
