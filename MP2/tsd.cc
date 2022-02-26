#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <deque>
#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <mutex>
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

using lock_t = std::unique_lock<std::mutex>;

class User {
public:
    std::string username;
    std::vector<std::string> following;
    std::vector<std::string> followers;

    // For each user in following, store their messages in timeline
    std::deque<csce438::Message> timeline;

    // Pointer to bidirectional stream
    grpc::ServerReaderWriter<Message, Message>* timeline_stream;

    // Mutex for modifying user data
    std::mutex mutex;
    // Mutex for timeline stream
    std::mutex timeline_mutex;

    User(std::string username,
         std::vector<std::string> following,
         std::vector<std::string> followers,
         std::deque<csce438::Message> timeline)
        : username(username)
        , following(following)
        , followers(followers)
        , timeline(timeline)
        , timeline_stream(nullptr)
        , mutex({})
        , timeline_mutex({}) {};

    void add_timeline_message(csce438::Message message)
    {
        // Presumption is that user mutex is locked already

        // I feel it would be more natural to push_back then pop_front so that newer
        // messages are towards the bottom.
        // Test cases have it in reverse order, i.e. older at bottom.
        timeline.push_front(message);

        // We want to store only the previous 20 timeline messages
        if (timeline.size() > 20)
            timeline.pop_back();
    }

    void send_timeline_message(csce438::Message const& message)
    {
        // Send message on stream if set
        if (timeline_stream == nullptr)
            return;

        auto stream_lock = lock_t { timeline_mutex };

        timeline_stream->Write(message);

        stream_lock.unlock();
    }
};

class SNSServiceImpl final : public SNSService::Service {
private:
    std::unordered_map<std::string, std::shared_ptr<User>> m_users;
    std::mutex m_mutex;

public:
    SNSServiceImpl()
        : m_users({})
    {
    }

    Status List(ServerContext* context, const Request* request, Reply* reply) override
    {
        auto username = request->username();
        auto lock = lock_t { m_mutex };
        auto users = std::vector<std::string> {};

        users.reserve(m_users.size());

        for (auto&& user : m_users)
            reply->add_all_users(user.first);

        for (auto&& follower : m_users[username]->followers)
            reply->add_following_users(follower);

        lock.unlock();

        return Status::OK;
    }

    Status Follow(ServerContext* context, const Request* request, Reply* reply) override
    {
        auto username = request->username();
        auto lock = lock_t { m_mutex };

        if (!request->arguments_size()) {
            // Target not supplied
            reply->set_msg("bad name");
            return Status::OK;
        }

        auto target_username = request->arguments(0);

        if (m_users.find(target_username) == m_users.end()) {
            // Target not exist
            reply->set_msg("bad name");
            return Status::OK;
        }

        auto const& user = m_users[username];
        auto const& target = m_users[target_username];

        auto user_lock = lock_t { user->mutex };
        auto target_lock = lock_t { target->mutex };

        if (std::find(user->following.begin(), user->following.end(), target_username) != user->following.end()) {
            // User already follows target
            reply->set_msg("duplicate");
            return Status::OK;
        }

        // Add target to user following, user to target followers
        user->following.push_back(target_username);
        target->followers.push_back(username);

        target_lock.unlock();
        user_lock.unlock();
        lock.unlock();

        return Status::OK;
    }

    Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override
    {
        auto username = request->username();
        auto lock = lock_t { m_mutex };

        if (!request->arguments_size()) {
            // Target not supplied
            reply->set_msg("bad name");
            return Status::OK;
        }

        auto target_username = request->arguments(0);

        if (m_users.find(target_username) == m_users.end()) {
            // Target does not exist
            reply->set_msg("bad name");
            return Status::OK;
        }

        // Remove user from target's followers
        auto const& target = m_users[target_username];
        auto target_lock = lock_t { target->mutex };
        auto pos = target->followers.end();

        if ((pos = std::find(target->followers.begin(), target->followers.end(), username)) == target->followers.end()) {
            // User was not following target
            reply->set_msg("bad name");
            return Status::OK;
        }

        target->followers.erase(pos);

        target_lock.unlock();

        // Remove target from user's following
        // We need not check if target in user's following since user is in followers of target
        auto const& user = m_users[username];
        auto user_lock = lock_t { user->mutex };
        pos = std::find(user->following.begin(), user->following.end(), target_username);

        m_users[username]->following.erase(pos);

        user_lock.unlock();
        lock.unlock();

        return Status::OK;
    }

    Status Login(ServerContext* context, const Request* request, Reply* reply) override
    {
        auto username = request->username();
        auto lock = lock_t { m_mutex };

        if (m_users.find(username) != m_users.end()) {
            // A user with the same username already exists
            reply->set_msg("duplicate");
            return Status::OK;
        }

        // By default a user follows themselves
        m_users[username] = std::make_shared<User>(username,
                                                   std::vector<std::string> {},
                                                   std::vector<std::string> { username },
                                                   std::deque<csce438::Message> {});

        lock.unlock();

        return Status::OK;
    }

    __attribute__((flatten)) Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override
    {
        // ------------------------------------------------------------
        // In this function, you are to write code that handles
        // receiving a message/post from a user, recording it in a file
        // and then making it available on his/her follower's streams
        // ------------------------------------------------------------
        auto timeline_message = csce438::Message {};
        auto lock = lock_t { m_mutex, std::defer_lock };

        while (true) {
            if (!stream->Read(&timeline_message))
                continue;

            lock.lock();

            auto username = timeline_message.username();
            auto pos = m_users.end();

            // I don't think we need to validate that a timeline message is from a valid user
            if ((pos = m_users.find(username)) == m_users.end())
                return Status::CANCELLED;

            // Get user ptr and lock
            auto const& user = m_users[username];
            auto user_lock = lock_t { user->mutex };

            if (user->timeline_stream == nullptr) {
                // Set stream if not already exists
                if (timeline_message.msg() != "0xFEE1DEAD") {
                    std::cerr << "something went terribly wrong\n";
                    exit(EXIT_FAILURE);
                }

                user->timeline_stream = stream;

                // Send accumulated timeline messages from before user entered timeline mode
                for (auto&& msg : user->timeline)
                    user->send_timeline_message(msg);

                user_lock.unlock();

                lock.unlock();
                continue;
            }

            // Iterate through all of user's followers, append message to timeline
            for (auto&& follower_username : user->followers) {
                auto const& follower = m_users[follower_username];

                if (follower_username == username) {
                    // We've locked user mutex already; don't deadlock by locking again
                    user->add_timeline_message(timeline_message);
                    continue;
                }

                auto follower_lock = lock_t { follower->mutex };

                follower->add_timeline_message(timeline_message);

                follower_lock.unlock();

                // send_timeline_message locks stream mutex for us
                follower->send_timeline_message(timeline_message);
            }

            user_lock.unlock();

            auto grpc_timestamp = timeline_message.timestamp();
            auto timestamp = google::protobuf::util::TimeUtil::TimestampToTimeT(grpc_timestamp);

            lock.unlock();
        }

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
