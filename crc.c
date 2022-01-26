#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>

#include "interface.h"

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
    // ------------------------------------------------------------
    // GUIDE 1:
    // In this function, you are supposed to parse a given command
    // and create your own message in order to communicate with
    // the server. Surely, you can use the input command without
    // any changes if your server understand it. The given command
    // will be one of the followings:
    //
    // CREATE <name>
    // DELETE <name>
    // JOIN <name>
    // LIST
    //
    // -  "<name>" is a chatroom name that you want to create, delete,
    // or join.
    //
    // - CREATE/DELETE/JOIN and "<name>" are separated by one space.
    // ------------------------------------------------------------

    if (!strncmp(command, "CREATE", 6)) {

    } else if (!strncmp(command, "DELETE", 6)) {

    } else if (!strncmp(command, "JOIN", 4)) {

    } else if (!strncmp(command, "LIST", 4)) {
    }

    // ------------------------------------------------------------
    // GUIDE 2:
    // After you create the message, you need to send it to the
    // server and receive a result from the server.
    // ------------------------------------------------------------

    // ------------------------------------------------------------
    // GUIDE 3:
    // Then, you should create a variable of Reply structure
    // provided by the interface and initialize it according to
    // the result.
    //
    // For example, if a given command is "JOIN room1"
    // and the server successfully created the chatroom,
    // the server will reply a message including information about
    // success/failure, the number of members and port number.
    // By using this information, you should set the Reply variable.
    // the variable will be set as following:
    //
    // Reply reply;
    // reply.status = SUCCESS;
    // reply.num_member = number;
    // reply.port = port;
    //
    // "number" and "port" variables are just an integer variable
    // and can be initialized using the message fomr the server.
    //
    // For another example, if a given command is "CREATE room1"
    // and the server failed to create the chatroom becuase it
    // already exists, the Reply varible will be set as following:
    //
    // Reply reply;
    // reply.status = FAILURE_ALREADY_EXISTS;
    //
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
    // ------------------------------------------------------------

    // REMOVE below code and write your own Reply.
    struct Reply reply;
    reply.status = SUCCESS;
    reply.num_member = 5;
    reply.port = 1024;
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
    // ------------------------------------------------------------
    // GUIDE 1:
    // In order to join the chatroom, you are supposed to connect
    // to the server using host and port.
    // You may re-use the function "connect_to".
    // ------------------------------------------------------------

    auto socketfd = connect_to(host, port);

    // ------------------------------------------------------------
    // GUIDE 2:
    // Once the client have been connected to the server, we need
    // to get a message from the user and send it to server.
    // At the same time, the client should wait for a message from
    // the server.
    // ------------------------------------------------------------

    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    // 1. To get a message from a user, you should use a function
    // "void get_message(char*, int);" in the interface.h file
    //
    // 2. To print the messages from other members, you should use
    // the function "void display_message(char*)" in the interface.h
    //
    // 3. Once a user entered to one of chatrooms, there is no way
    //    to command mode where the user  enter other commands
    //    such as CREATE,DELETE,LIST.
    //    Don't have to worry about this situation, and you can
    //    terminate the client program by pressing CTRL-C (SIGINT)
    // ------------------------------------------------------------
}
