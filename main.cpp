#include <csignal>
#include <iostream>
#include <optional>
#include <string>

#include "Server.h"
#include "KeyValueStore.h"

// sample usage, primary and replica on one machine (separate aof files so they don't share one):
//   ./redisclone --port 6379 --aof primary.aof
//   ./redisclone --port 6380 --aof replica.aof --replicaof 127.0.0.1 6379
int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN); //send() to a dead replica/client returns an error instead of killing the server

    uint16_t port = 6379;
    std::string aofPath = "appendonly.aof";
    std::optional<ReplicaOf> primary;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--aof" && i + 1 < argc) {
            aofPath = argv[++i];
        } else if (arg == "--replicaof" && i + 2 < argc) {
            primary = ReplicaOf{argv[i + 1], static_cast<uint16_t>(std::stoi(argv[i + 2]))};
            i += 2;
        } else {
            std::cerr << "usage: redisclone [--port N] [--aof path] [--replicaof host port]\n";
            return 1;
        }
    }

    KeyValueStore store{aofPath};
    Server server{port, store, primary};
    server.run();
}
