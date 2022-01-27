# MP1

C/C++ chat room client and server.

## Design
For MP1, I am aiming for a high throughput chat application.

### Protocol
TCP is used as in a chat application, minimization of data loss and preservation of the order is important, despite the overhead from the SYN/ACK and FIN/ACK messages.

On the topic of TCP, I hope to improve message throughput by modifing the socket options via `setsockopt()`.
One possible optimization is to set the `TCP_NODELAY` option on a per socket basis to disable Nagle's algorithm.

TCP over IPv4 has a 40-byte header, which leads me to the two (strong) assumptions:
1. the size of commands and chat messages over this program will never be much greater than the header size,
2. our connections between the client and server will always have adequate bandwidth.

From assumption 1, we can take for granted that most data communicated between the client and server will be transferred in tinygrams; so it stands to reason that disabling Nagle's algorithm will improve our performance.

On the flip side, this leaves us susceptible to the following:
1. silly window syndrome if either the client or server processes requests too slowly; however, I believe this to be a nonissue due to the simplicity of the program and parallelization.
2. congestion collapse; from assumption 2: a nonissue.


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
