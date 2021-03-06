#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
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

        if (reply.status == SUCCESS) {
            touppercase(command, strlen(command) - 1);
            if (strncmp(command, "JOIN", 4) == 0) {
                printf("Now you are in the chatmode\n");
                process_chatmode(argv[1], reply.port);
            }
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
    auto buffer = std::make_unique<char[]>(BUFSIZ);
    auto offset = 0;
    auto message = MessageType::INVALID;

    // strncasecmp is non-POSIX
    // Compare ignore case the string to known command keywords
    // Offset assumes a space after these commands
    if (!strncasecmp(command, "CREATE", 6)) {
        message = CREATE;
        offset = 7;
    } else if (!strncasecmp(command, "DELETE", 6)) {
        message = DELETE;
        offset = 7;
    } else if (!strncasecmp(command, "JOIN", 4)) {
        message = JOIN;
        offset = 5;
    } else if (!strncasecmp(command, "LIST", 4)) {
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

    // Extract status code from server
    reply.status = reinterpret_cast<Status&>(*cursor);

    cursor += sizeof(Status);

    auto data = uint32_t {};

    if (message == JOIN) {
        // Extract port and number of members from server response
        reply.port = reinterpret_cast<decltype(reply.port)&>(*cursor);
        cursor += sizeof(reply.port);

        reply.num_member = reinterpret_cast<decltype(reply.num_member)&>(*cursor);
        cursor += sizeof(reply.num_member);
    } else if (message == LIST) {
        // After the status code, follows the list of chatroom names delimited by commas
        auto list = std::string { cursor };

        if (!list.size())
            list = "empty";

        strcpy(reply.list_room, list.c_str());

        // Terminate string properly
        reply.list_room[MAX_DATA - 1] = '\0';
    }

    return reply;
}

/*
 * Get into the chat mode
 *
 * @parameter host     host address
 * @parameter port     port
 */
__attribute__((flatten)) void process_chatmode(const char* host, const int port)
{
    auto socketfd = connect_to(host, port);
    auto kill = bool {};

    // To improve throughput (but not latency) aggressively buffer packets
    auto unused = 1;
    setsockopt(socketfd, IPPROTO_TCP, TCP_CORK, &unused, sizeof(decltype(TCP_NODELAY)));

    // Threads are overkill but I don't remember how to use epoll()
    auto listener = std::thread([socketfd, &kill]() -> void {
        auto buffer = std::make_unique<char[]>(BUFSIZ);

        while (true) {
            auto bytes = recv(socketfd, buffer.get(), BUFSIZ, 0);

            if (bytes < 0) {
                if (errno == ECONNRESET || errno == EPIPE) {
                    // Chatroom closed or server died.
                    close(socketfd);
                    kill = true;
                    return;
                }
                perror("recv()");
                continue;
            }

            // Check if first 32-bits match DELETE message
            if (reinterpret_cast<uint32_t&>(*buffer.get()) == static_cast<uint32_t>(MessageType::DELETE)) {
                // Close socket and return to command mode if so
                close(socketfd);
                kill = true;
                return;
            }

            // If new message is shorter than previous message then we will start writing
            // the previous message without setting the sentinel value.
            buffer.get()[bytes] = '\0';

            // Write received message into STDOUT
            display_message(buffer.get());
            std::cout << '\n';
        }
    });

    listener.detach();

    auto buffer = std::make_unique<char[]>(BUFSIZ);

    // Don't immediately fgets() since it blocks entire thread; if a DELETE message is sent while we are blocked
    // the thread won't terminate until after the user sends a new line.

    // We use select() with a 100ms timeout to detect input, so that if kill is set, we terminate thread as
    // quickly as possible, otherwise we check for input and fgets() if data is available.
    auto tv = timeval {
        .tv_sec = 0,
        .tv_usec = 1'000
    };

    while (true) {
        // select() setup code
        auto fds = fd_set {};

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        auto ret = select(1, &fds, NULL, NULL, &tv);

        if (ret < 0) {
            perror("select()");
            return;
        }

        if (ret) {
            // Get message using fgets()
            get_message(buffer.get(), BUFSIZ);

            send(socketfd, buffer.get(), strlen(buffer.get()), 0);
        }

        if (kill)
            return;
    }
}
