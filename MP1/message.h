#pragma once

enum MessageType { CREATE,   // Create new room              (client  -> server)
                   DELETE,   // Delete room                  (client <-> server)
                   JOIN,     // Join a room                  (client  -> server)
                   LIST,     // List all rooms               (client  -> server)
                   RESPONSE, // Response from other commands (server  -> client)
                   INVALID
                   };
