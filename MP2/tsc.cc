#include "client.h"
#include "sns.grpc.pb.h"
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
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

        if (status.ok())
            if (response.msg() == "bad name")
                reply.comm_status = FAILURE_INVALID_USERNAME;
            else if (response.msg() == "duplicate")
                reply.comm_status = FAILURE_ALREADY_EXISTS;
    } else if (input.rfind("UNFOLLOW", 0) == 0) {
        request.add_arguments(input.substr(9));
        status = stub_->UnFollow(&context, request, &response);

        if (status.ok())
            if (response.msg() == "bad name")
                reply.comm_status = FAILURE_INVALID_USERNAME;
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
        reply.comm_status = FAILURE_INVALID;
        return reply;
    }

    reply.grpc_status = status;
    return reply;
}

void Client::processTimeline()
{
    auto context = grpc::ClientContext {};
    auto timeline = stub_->Timeline(&context);

    auto reader = std::thread([&timeline]() -> void {
        // Separate thread to read new timeline messages
        auto timeline_message = csce438::Message {};

        while (true) {
            if (!timeline->Read(&timeline_message))
                continue;

            auto grpc_timestamp = timeline_message.timestamp();
            auto timestamp = google::protobuf::util::TimeUtil::TimestampToTimeT(grpc_timestamp);

            displayPostMessage(timeline_message.username(), timeline_message.msg(), timestamp);
        }
    });
    
    // Main thread handles blocking I/O

    // Setup timeline message
    auto timeline_message = csce438::Message {};
    timeline_message.set_username(username);

    // Write initialization message to establish stream in server user object
    // Server side will assert message is "0xFEE1DEAD"
    timeline_message.set_msg("0xFEE1DEAD");
    timeline->Write(timeline_message);

    // Spin up async reader
    reader.detach();
    while (true) {
        // No exit aside from SIGINT per instructions
        auto message = getPostMessage();

        timeline_message.set_msg(message);

        timeline->Write(timeline_message);
    }
}
