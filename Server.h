//
// Created by Daniel Elbaz on 8/28/26.
//

#ifndef REDISCLONE_SERVER_H
#define REDISCLONE_SERVER_H
#include <cstdint>
#include <optional>
#include <string>

#include "RespParser.h"
#include "KeyValueStore.h"
#include "ReplicaSet.h"

struct ReplicaOf { // where a replica's primary lives
    std::string host;
    uint16_t port;
};

class Server {
public:
    Server(uint16_t port, KeyValueStore& kv, std::optional<ReplicaOf> primary = std::nullopt);
    int run();
private:
    void handleClient(int client_fd);
    void handleExpiration();
    void connectToPrimary();
    void replicateFrom(int primary_fd);

    uint16_t server_port_;
    KeyValueStore& kv_;
    std::optional<ReplicaOf> primary_; // set only when running as a replica
    ReplicaSet replicas_; // connected replica sockets, only used when we're the primary
};


#endif //REDISCLONE_SERVER_H