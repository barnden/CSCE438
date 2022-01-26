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

### Parallelization
I decided to create a multithreadded server in order to handle requests from multiple clients.

The server will generate a new thread for each client.

The messages and commands will be sent over the same socket.
