# MP1

C/C++ chat room client and server.

## Design
For MP1, I am aiming for a high throughput chat application.

### Protocol
I chose to use TCP instead of UDP, in spite of the additional overhead from SYN/ACK and FIN/ACK.
This is because in a chat application, it is important to minimize data (packet) loss as well as the order of messages.

#### Optimizing TCP
On the topic of TCP, I hope to improve message volume by disabling Nagle's algorithm, which reduces the overhead from buffering.
This leaves us susceptible to the silly window syndrome if either the client or server processes requests too slowly, but I think this should be a nonissue.

We can disable Nagle's on a per socket basis by setting the `TCP_NODELAY` option via `setsockopt`.

### Messages
The client and server will communicate via messages, first by sending a `uint32_t` enum value:
```
enum MessageType { CREATE, DELETE, JOIN, LIST, MESSAGE, RESPONSE };
```

After, the client/server will read from the socket up until some sentinel value.

### Server

#### Parallelization
The server will have a main thread that listens to the specified port from the command line.

For each client connection to the main socket, a new thread will be created to process that client's requests.

#### Database
To improve performance, I am using a `std::unordered_map` to get O(1) access.
