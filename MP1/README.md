# MP1

C/C++ chat room client and server.

## Design
For MP1, I am aiming for a high throughput chat application.

### Protocol
Despite the overhead from SYN/ACK and FIN/ACK from TCP; I chose TCP instead of UDP as I did not wish to implement my own system to deal with packet loss and arrival order.

On the topic of TCP, I hope to improve message throughput by modifying the socket options via `setsockopt()`.
There are two optimizations on opposite extremes: `TCP_NODELAY` and `TCP_CORK`.

With `TCP_NODELAY`, we can immediately send out packets without waiting for Nagle's algorithm to accumulate them into a buffer.
I suspect that `TCP_NODELAY` will improve application responsiveness, as our payload will never be much greater than the headers on each TCP packet (40 bytes for IPv4).
On the flip side, this leaves us susceptible to the silly window syndrome if our server/client processes requests too slowly, or congestion collapse if our connection isn't good enough.

While `TCP_CORK` does the other extreme by accumulating as many tinygrams into one packet within a 200ms window.
We could argue that a chat application need not be low latency; the only thing that should matter is that messages are transmitted, not their arrival time.
By aggressively buffering the tinygrams, we reduce additional overhead from TCP, as we transmit data when the buffer is filled not when we receive acknowledgement from the previous packet.

### Client-Server Communication

#### Command Mode
The server will listen on the specified port for incoming client connections.
On accepting a connection, the server will spawn a new thread to handle commands from the client.

Each command will be prepended with a 32-bit value from the `MessageType` enum:
```
enum MessageType { CREATE, DELETE, JOIN, LIST, RESPONSE };
```

The `CREATE`, `DELETE`, `JOIN`, `LIST` types will be sent from client to server.
These commands will be followed by a variable length null-terminated string from the client, capped at 224 bytes.

The `RESPONSE` type sent from server to client.
The data that follows the `RESPONSE` value depends on the client's message type:
- `CREATE` and `DELETE` is followed by a single 32-bit value from the `Status` enum
- `JOIN` is followed by two 32-bit values: `port` and `members`
- `LIST` is followed a null-terminated string.

#### Chat Mode
After sending the `JOIN` message, the client will await for the port number from the `RESPONSE` message from the server.
The client will then establish a connection to the server over said port.
Now in chat mode, the client will await for user input; upon receiving input the client will send the null-terminated string over the socket (capped at `BUFSIZ` bytes).
The server thread for the chat room upon receiving a message will then store the message in a buffer, and broadcast it to every client connected to the room.

### Server

#### Parallelization
The server on the main thread will have a socket binded to the port specified.
Each client connection will be handled on a separate thread.

Upon receiving `JOIN` message, the server will create a new thread to listen on the chatroom's port and accept client connections.
When a client attempts to connect to the chatroom yet another thread is spawned, this time it will await until the client sends a chat message.

As there is no method for a user to exit chatmode, we can also close the initial command socket.
#### Database
To improve performance, I am using a `std::unordered_map` to get O(1) access.

## Performance
After completing MP1, I sought to see if I was correct in assuming that either `TCP_CORK` or `TCP_NODELAY` had any measurable impact on throughput.

### Methodology
The `crc` and `crsd` binaries were compiled using the `-O3` flag against C++17 on gcc 11.1.

To measure the throughput, I did the following:
1. Create a server on using `./crsd 8080`
2. Join from a client using `./crc localhost 8080`, and manually inputting `create r1`.
3. Execute `yes "join r1" | ./crc localhost 8080`
    - The `yes` command will output to STDOUT `join r1` repeatedly (on my system, about 6.6 GiB/s)
4. Execute `echo "join r1" | ./crc localhost 8080 | pv >/dev/null`
    - The echo will pipe into `crc`, making it join chatroom `r1`.
    - From step 3, a client will be transmitting many `join r1` chat messages a second, this is output to this client's STDOUT.
    - This client's STDOUT is redirected into `pv` which will measure the throughput of the chat application.
### Benchmarks

| Default Socket Settings | `TCP_NODELAY` | `TCP_CORK` |
| - | - | - |
| 17.2 MiB/s | 5.82 MiB/s | 26.7 MiB/s |

As we see, `TCP_CORK` was able to attain the highest throughput.
