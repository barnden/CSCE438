#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "sns.grpc.pb.h"

using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::SNSService;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

struct User {
    std::vector<std::string> following;
    std::vector<std::string> followers;
};

class SNSServiceImpl final : public SNSService::Service {
private:
    std::unordered_map<std::string, User> m_users;

public:
    SNSServiceImpl()
        : m_users({})
    {
    }

    Status List(ServerContext* context, const Request* request, Reply* reply) override
    {
        // ------------------------------------------------------------
        // In this function, you are to write code that handles
        // LIST request from the user. Ensure that both the fields
        // all_users & following_users are populated
        // ------------------------------------------------------------
        return Status::OK;
    }

    Status Follow(ServerContext* context, const Request* request, Reply* reply) override
    {
        // ------------------------------------------------------------
        // In this function, you are to write code that handles
        // request from a user to follow one of the existing
        // users
        // ------------------------------------------------------------
        return Status::OK;
    }

    Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override
    {
        // ------------------------------------------------------------
        // In this function, you are to write code that handles
        // request from a user to unfollow one of his/her existing
        // followers
        // ------------------------------------------------------------
        return Status::OK;
    }

    Status Login(ServerContext* context, const Request* request, Reply* reply) override
    {
        // ------------------------------------------------------------
        // In this function, you are to write code that handles
        // a new user and verify if the username is available
        // or already taken
        // ------------------------------------------------------------
        auto username = request->username();

        if (m_users.find(username) != m_users.end())
            return Status::CANCELLED;

        m_users[username] = User { {}, {} };

        return Status::OK;
    }

    Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override
    {
        // ------------------------------------------------------------
        // In this function, you are to write code that handles
        // receiving a message/post from a user, recording it in a file
        // and then making it available on his/her follower's streams
        // ------------------------------------------------------------
        return Status::OK;
    }
};

void RunServer(std::string port_no)
{
    // ------------------------------------------------------------
    // In this function, you are to write code
    // which would start the server, make it listen on a particular
    // port number.
    // ------------------------------------------------------------
    auto address = std::string { "0.0.0.0:" } + port_no;
    auto service = SNSServiceImpl();

    ServerBuilder builder;

    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    server->Wait();
}

int main(int argc, char** argv)
{
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            port = optarg;
            break;
        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }
    RunServer(port);
    return 0;
}
