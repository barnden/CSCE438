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
#include <unordered_map>
#include <vector>

#include "interface.h"
#include "message.h"

void handle_room(std::string room_name, int socket);
int get_socket(std::string port, bool no_fail);

// Ports 1024 - 65534 are not restricted to superuser
// Keep track of the next port number that we have not attempted to use
auto g_next_port = 1024;

// Mutex associated with g_next_port
auto g_port_mutex = std::mutex {};

// Mutex associated with g_chatrooms
auto g_room_mutex = std::mutex {};

class Room {
public:
    int m_port;
    int m_members;
    int m_socket;
    std::thread m_handler;
    std::vector<int> m_sockets;

    Room(std::string const& room_name)
        : m_members(0)
        , m_socket(0)
    {
        // The room mutex should be locked before entering this constructor.

        auto port_lock = std::unique_lock<std::mutex>(g_port_mutex);

        // Keep trying to open a socket for the chatroom on g_next_port
        // Technically we should not be naively incrementing g_next_port in case of overflow,
        // but I dont think we're creating over 2^16 chatrooms anytime soon
        while ((m_socket = get_socket(std::to_string(g_next_port), true)) < 0)
            g_next_port++;

        m_port = g_next_port;

        port_lock.unlock();

        // New thread to handle the individual chat room
        m_handler = std::thread(handle_room, room_name, m_socket);
        m_handler.detach();
    }

    ~Room()
    {
        // Close socket
        close(m_socket);
    }
};

// In memory "database" of chat rooms
auto g_chatrooms = std::unordered_map<std::string, std::unique_ptr<Room>>();

/*
 * Create a socket to listen to on a given port
 *
 * @parameter port      port given by command line argument
 * @parameter no_fail   should we exit program on failure to bind
 *
 * @return socket file descriptor
 */
int get_socket(std::string port, bool no_fail = false)
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

        if (no_fail)
            return -1;

        perror("bind()");
        exit(EXIT_FAILURE);
    }

    auto unused = 1;
    setsockopt(socketfd, IPPROTO_TCP, TCP_CORK, &unused, sizeof(decltype(TCP_NODELAY)));

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

// handle_chat is a very hot function, we can aggresively inline with flatten
__attribute__((flatten)) void handle_chat(std::string const& room_name, int socket)
{
    auto buffer = std::make_unique<char[]>(BUFSIZ);
    auto room_lock = std::unique_lock<std::mutex>(g_room_mutex, std::defer_lock);

    while (true) {
        auto bytes = recv(socket, buffer.get(), BUFSIZ, 0);

        if (bytes < 0) {
            if (errno == ECONNRESET || errno == EPIPE) {
                close(socket);
                return;
            }

            perror("recv(): socket");
            return;
        }

        room_lock.lock();

        // Multicast message to connected clients
        if (g_chatrooms.find(room_name) == g_chatrooms.end()) {
            // Room has been deleted?
            return;
        }

        auto& room = g_chatrooms[room_name];
        auto peer = room->m_sockets.begin();

        while (peer != room->m_sockets.end()) {
            if (*peer == socket) {
                peer++;
                continue;
            }

            if (send(*peer, buffer.get(), bytes, 0) < 0) {
                if (errno == ECONNRESET || errno == EPIPE) {
                    close(*peer);
                    room->m_members--;
                    peer = room->m_sockets.erase(peer);
                    continue;
                }

                perror("send(): chat");
                exit(errno);
            }

            peer++;
        }

        room_lock.unlock();

        memset(buffer.get(), 0, BUFSIZ);
    }
}

void handle_room(std::string room_name, int socket)
{
    auto client = sockaddr_storage {};
    auto sin_size = socklen_t { sizeof(client) };

    // Continuously accept client connections to the chatroom
    while (true) {
        auto client_socket = accept(socket, reinterpret_cast<sockaddr*>(&client), &sin_size);

        if (client_socket < 0) {
            // Acknowledge failure to accept; don't exit out
            perror("accept()");
            continue;
        }

        auto room_lock = std::unique_lock<std::mutex>(g_room_mutex);

        auto& room = g_chatrooms[room_name];

        room->m_sockets.push_back(client_socket);
        room->m_members++;

        room_lock.unlock();

        // Spin up new thread to process chat messages
        auto t = std::thread(handle_chat, room_name, client_socket);
        t.detach();
    }
}

void handle_creation(int client, std::string const& room_name)
{
    auto buffer = std::make_unique<char[]>(MAX_DATA);
    auto status = Status::FAILURE_UNKNOWN;

    auto room_lock = std::unique_lock<std::mutex>(g_room_mutex);

    if (g_chatrooms.find(room_name) != g_chatrooms.end()) {
        // Do nothing; chatroom already exists
        status = Status::FAILURE_ALREADY_EXISTS;
        room_lock.unlock();
    } else {
        g_chatrooms[room_name] = std::make_unique<Room>(room_name);

        // Done with g_chatrooms
        room_lock.unlock();

        status = Status::SUCCESS;
    }

    // Only send the status of the operation; no information about the created room
    // Clients should send a separate JOIN command to join the chat room
    auto message = MessageType::RESPONSE;

    memcpy(buffer.get(), &message, sizeof(message));
    memcpy(buffer.get() + sizeof(message), &status, sizeof(status));

    send(client, buffer.get(), sizeof(message) + sizeof(status), 0);
}

void handle_deletion(int client, std::string const& room_name)
{
    auto buffer = std::make_unique<char[]>(MAX_DATA);

    auto status = Status::FAILURE_UNKNOWN;
    auto message = MessageType::DELETE;

    auto room_lock = std::unique_lock<std::mutex>(g_room_mutex);

    if (g_chatrooms.find(room_name) == g_chatrooms.end()) {
        // If chatroom does not exist, send not exists message
        status = Status::FAILURE_NOT_EXISTS;
        room_lock.unlock();
    } else {
        auto& room = g_chatrooms[room_name];

        // Copy DELETE message into buffer
        memcpy(buffer.get(), &message, sizeof(message));
        buffer[sizeof(message)] = '\0';

        // Stop accepting new connections by killing chatroom thread
        room->m_handler.~thread();

        for (auto&& peer : room->m_sockets) {
            // Send DELETE message to each client
            if (send(peer, buffer.get(), sizeof(message) + 1, 0) < 0) {
                perror("send(): deletion");
                continue;
            }

            // Close socket
            close(peer);
        }

        // Delete the chatroom
        g_chatrooms.erase(room_name);

        room_lock.unlock();

        status = Status::SUCCESS;
    }

    // Only send the status of the operation
    message = MessageType::RESPONSE;
    memcpy(buffer.get(), &message, sizeof(message));
    memcpy(buffer.get() + sizeof(message), &status, sizeof(status));

    send(client, buffer.get(), sizeof(message) + sizeof(status), 0);
}

void handle_join(int client, std::string const& room_name)
{
    auto buffer = std::make_unique<char[]>(MAX_DATA);

    auto status = Status::FAILURE_UNKNOWN;
    auto message = MessageType::DELETE;
    auto message_length = sizeof(message) + sizeof(status);

    auto room_lock = std::unique_lock<std::mutex>(g_room_mutex);

    if (g_chatrooms.find(room_name) == g_chatrooms.end()) {
        // If chatroom does not exist, send not exists message
        status = Status::FAILURE_NOT_EXISTS;
        room_lock.unlock();
    } else {
        // If chatroom does exist, we respond by sending the port number and number of connected clients in the chat room
        // It is then up to the client to create a new connection over the specified port
        auto& room = g_chatrooms[room_name];
        auto port = room->m_port;
        auto members = room->m_members;

        room_lock.unlock();

        auto cursor = buffer.get() + message_length;

        memcpy(cursor, &port, sizeof(port));
        memcpy(cursor + sizeof(port), &members, sizeof(members));

        message_length += sizeof(port) + sizeof(members);

        status = Status::SUCCESS;
    }

    // Only send the status of the operation
    message = MessageType::RESPONSE;
    memcpy(buffer.get(), &message, sizeof(message));
    memcpy(buffer.get() + sizeof(message), &status, sizeof(status));

    send(client, buffer.get(), message_length, 0);
}

void handle_list(int client)
{
    // The string can be at most MAX_DATA, which is preceded by 2 32-bit integers (message type and status)
    auto buffer = std::make_unique<char[]>(MAX_DATA + 64);

    auto message = MessageType::RESPONSE;
    auto status = Status::SUCCESS;

    // Generate a string containing the names of all chatrooms, delimited with a comma
    auto room_lock = std::unique_lock<std::mutex>(g_room_mutex);
    auto rooms = std::string {};

    // The expected output has a trailing comma
    for (auto&& pair : g_chatrooms)
        rooms += pair.first + ",";

    room_lock.unlock();

    // Copy relevant data into buffer
    auto cursor = buffer.get();

    memcpy(cursor, &message, sizeof(message));
    memcpy(cursor += sizeof(message), &status, sizeof(status));
    strcpy(cursor += sizeof(status), rooms.c_str());

    // Ensure null-termination
    buffer[MAX_DATA + 63] = '\0';

    send(client, buffer.get(), sizeof(message) + sizeof(status) + rooms.size(), 0);
}

void handle_client(int client)
{
    auto buffer = std::make_unique<char[]>(MAX_DATA);
    auto message = MessageType::RESPONSE;

    while (true) {
        auto bytes = recv(client, buffer.get(), MAX_DATA, 0);

        if (bytes <= 0)
            break;

        auto type = reinterpret_cast<MessageType&>(*buffer.get());
        auto room = std::string { buffer.get() + sizeof(MessageType) };

        switch (type) {
        case CREATE:
            handle_creation(client, room);
            break;
        case DELETE:
            handle_deletion(client, room);
            break;
        case JOIN:
            handle_join(client, room);
            return;
        case LIST:
            handle_list(client);
            break;
        default:
            // We should not get any other message type on the main client socket
            // Send to client a invalid command message

            auto status = Status::FAILURE_INVALID;

            memset(buffer.get(), 0, MAX_DATA);
            memcpy(buffer.get(), &message, sizeof(message));
            memcpy(buffer.get() + sizeof(message), &status, sizeof(status));
            send(client, buffer.get(), MAX_DATA, 0);
            break;
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: enter port number\n";
        return EXIT_FAILURE;
    }

    // Bind to the port from command line arguments
    auto server = get_socket(std::string { argv[1] });

    auto client = sockaddr_storage {};
    auto sin_size = socklen_t { sizeof(client) };

    // Continuously accept client connections
    while (true) {
        auto client_socket = accept(server, reinterpret_cast<sockaddr*>(&client), &sin_size);

        if (client_socket < 0) {
            perror("accept()");
            return EXIT_FAILURE;
        }

        // Handle communication with client asynchronously
        auto t = std::thread(handle_client, client_socket);
        t.detach();
    }

    return EXIT_SUCCESS;
}
