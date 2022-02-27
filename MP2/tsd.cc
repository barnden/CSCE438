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
private:
    std::ofstream m_file;

public:
    // These member variables shouldn't be public but I'm too lazy to refactor

    std::string username;
    bool logged_in;
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
        , logged_in(false)
        , following(following)
        , followers(followers)
        , timeline(timeline)
        , timeline_stream(nullptr)
        , mutex({})
        , timeline_mutex({}) {};

    static std::shared_ptr<User> from_file(std::string username)
    {
        // Create user object from file
        auto file = std::ifstream(username + ".usr");

        if (!file.is_open()) {
            std::cerr << "invalid username " << username << '\n';
            exit(EXIT_FAILURE);
        }

        auto following = std::vector<std::string> {};
        auto followers = std::vector<std::string> {};
        auto timeline = std::deque<csce438::Message> {};

        auto stage = 1;
        auto line = std::string {};

        file >> line;

        while (std::getline(file, line)) {
            // Magic numbers are used to denote which stage we are in
            if (line == "\x1B\xAD\xFE\xED") {
                stage = 2;
                continue;
            }

            if (line == "\xC0\x01\xD0\x0D") {
                stage = 3;
                continue;
            }

            if (line == "\x12\x04\x57\xBE\xEF") {
                stage = 4;
                continue;
            }

            // Depending on stage, add data
            switch (stage) {
            case 2:
                followers.push_back(line);
                break;
            case 3:
                following.push_back(line);
                break;
            case 4: {
                // Read timeline message from file
                auto message = csce438::Message {};

                message.set_username(line);

                std::getline(file, line);
                message.set_msg(line);

                std::getline(file, line);
                auto timestamp = new google::protobuf::Timestamp();
                google::protobuf::util::TimeUtil::FromString(line, timestamp);
                message.set_allocated_timestamp(timestamp);

                timeline.push_back(message);
                break;
            }
            }
        }

        return std::move(std::make_shared<User>(username, following, followers, timeline));
    }

    void add_timeline_message(csce438::Message message)
    {
        // [IMPORTANT] Presumption is that user mutex is locked already

        // I feel it would be more natural to push_back then pop_front so that newer
        // messages are towards the bottom.
        // Test cases have it in reverse order, i.e. older at bottom.
        timeline.push_front(message);

        // We want to store only the previous 20 timeline messages
        if (timeline.size() > 20)
            timeline.pop_back();

        save_user_state();
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

    void save_user_state()
    {
        // [IMPORTANT] Presumption is that user mutex is locked already

        // This is terribly inefficent as we open the file on each call, clearing and rewriting entire file.

        // [IMPORTANT] User file is opened with TRUNC on each save.
        m_file.open(username + ".usr", std::ios::trunc);

        m_file << username << '\n';

        // 0x1BADFEED indicates beginning of followers
        m_file << "\x1B\xAD\xFE\xED\n";
        for (auto&& follower : followers)
            m_file << follower << '\n';

        // 0xC001D00D indicates beginning of following
        m_file << "\xC0\x01\xD0\x0D\n";
        for (auto&& followee : following)
            m_file << followee << '\n';

        // Roastbeef (0x120457BEEF) indicates beginning timeline
        m_file << "\x12\x04\x57\xBE\xEF\n";
        for (auto&& message : timeline) {
            m_file << message.username() << '\n'
                   << message.msg() // we expect newline at end of msg
                   << message.timestamp() << '\n';
        }

        m_file.flush();
        m_file.close();
    }
};

class SNSServiceImpl final : public SNSService::Service {
private:
    std::unordered_map<std::string, std::shared_ptr<User>> m_users;
    std::mutex m_mutex;
    std::ofstream m_file;

public:
    SNSServiceImpl()
        : m_users({})
    {
        m_file = std::ofstream("server.dat", std::ios::app);

        auto file = std::ifstream("server.dat");
        auto line = std::string {};

        while (file >> line)
            m_users[line] = User::from_file(line);
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

        user->save_user_state();
        target->save_user_state();

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
        target->save_user_state();

        target_lock.unlock();

        // Remove target from user's following
        // We need not check if target in user's following since user is in followers of target
        auto const& user = m_users[username];
        auto user_lock = lock_t { user->mutex };
        pos = std::find(user->following.begin(), user->following.end(), target_username);

        user->following.erase(pos);
        user->save_user_state();

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

            if (m_users[username]->logged_in) {
                // Multiple clients cannot have the same username
                reply->set_msg("duplicate");
                return Status::OK;
            }
        } else {
            // By default a user follows themselves
            m_users[username] = std::make_shared<User>(username,
                                                       std::vector<std::string> { username },
                                                       std::vector<std::string> { username },
                                                       std::deque<csce438::Message> {});

            m_users[username]->save_user_state();

            // Store username in server.dat
            m_file << username << '\n';
            m_file.flush();
        }

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
