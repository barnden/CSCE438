#include "client.h"
#include "sns.grpc.pb.h"
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

class Client : public IClient {
public:
    Client(const std::string& hname,
           const std::string& uname,
           const std::string& p)
        : hostname(hname)
        , username(uname)
        , port(p)
    {
    }

protected:
    virtual int connectTo();
    virtual IReply processCommand(std::string& input);
    virtual void processTimeline();

private:
    std::string hostname;
    std::string username;
    std::string port;

    // You can have an instance of the client stub
    // as a member variable.
    // std::unique_ptr<NameOfYourStubClass::Stub> stub_;
    std::unique_ptr<csce438::SNSService::Stub> stub_;
};

int main(int argc, char** argv)
{
    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1) {
        switch (opt) {
        case 'h':
            hostname = optarg;
            break;
        case 'u':
            username = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

int Client::connectTo()
{
    auto channel = grpc::CreateChannel(hostname + ':' + port, grpc::InsecureChannelCredentials());
    stub_ = std::move(csce438::SNSService::NewStub(channel));

    auto reply = csce438::Reply {};
    auto context = grpc::ClientContext {};
    auto request = csce438::Request {};

    request.set_username(username);

    auto status = stub_->Login(&context, request, &reply);

    if (status.ok())
        return 1;

    return -1;
}

IReply Client::processCommand(std::string& input)
{
    // ------------------------------------------------------------
    // HINT: How to set the IReply?
    // Suppose you have "Follow" service method for FOLLOW command,
    // IReply can be set as follow:
    //
    //     // some codes for creating/initializing parameters for
    //     // service method
    //     IReply ire;
    //     grpc::Status status = stub_->Follow(&context, /* some parameters */);
    //     ire.grpc_status = status;
    //     if (status.ok()) {
    //         ire.comm_status = SUCCESS;
    //     } else {
    //         ire.comm_status = FAILURE_NOT_EXISTS;
    //     }
    //
    //      return ire;
    //
    // IMPORTANT:
    // For the command "LIST", you should set both "all_users" and
    // "following_users" member variable of IReply.
    // ------------------------------------------------------------

    auto reply = IReply {};

    // Common to all rpc
    auto context = grpc::ClientContext {};
    auto status = grpc::Status {};
    auto request = csce438::Request {};
    auto response = csce438::Reply {};
    request.set_username(username);
    reply.comm_status = SUCCESS;

    if (input.rfind("FOLLOW", 0) == 0) {
        request.add_arguments(input.substr(7));
        status = stub_->Follow(&context, request, &response);

        if (!status.ok())
            reply.comm_status = FAILURE_INVALID_USERNAME;
    } else if (input.rfind("UNFOLLOW", 0) == 0) {
        request.add_arguments(input.substr(9));
        status = stub_->UnFollow(&context, request, &response);

        if (!status.ok())
            reply.comm_status = FAILURE_INVALID;
    } else if (input.rfind("LIST", 0) == 0) {
        status = stub_->List(&context, request, &response);

        if (status.ok()) {
            reply.all_users = std::vector<std::string> { response.all_users().begin(), response.all_users().end() };
            reply.following_users = std::vector<std::string> { response.following_users().begin(), response.following_users().end() };
        } else {
            reply.comm_status = FAILURE_UNKNOWN;
        }
    } else if (input.rfind("TIMELINE", 0) == 0) {
        processTimeline();
    } else {
        reply.comm_status = FAILURE_NOT_EXISTS;
        return reply;
    }

    reply.grpc_status = status;
    return reply;
}

void Client::processTimeline()
{
    // ------------------------------------------------------------
    // In this function, you are supposed to get into timeline mode.
    // You may need to call a service method to communicate with
    // the server. Use getPostMessage/displayPostMessage functions
    // for both getting and displaying messages in timeline mode.
    // You should use them as you did in hw1.
    // ------------------------------------------------------------
    auto context = grpc::ClientContext {};
    auto timeline = stub_->Timeline(&context);
    auto timeline_message = csce438::Message {};

    while (timeline->Read(&timeline_message)) {
        auto username = timeline_message.username();
        auto message = timeline_message.msg();
        auto timestamp = timeline_message.timestamp();
    }
    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    //
    // Once a user enter to timeline mode , there is no way
    // to command mode. You don't have to worry about this situation,
    // and you can terminate the client program by pressing
    // CTRL-C (SIGINT)
    // ------------------------------------------------------------
}
