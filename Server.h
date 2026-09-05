//
// Created by Daniel Elbaz on 8/28/26.
//

#ifndef REDISCLONE_SERVER_H
#define REDISCLONE_SERVER_H
#include <cstdint>

#include "RespParser.h"
#include "KeyValueStore.h"

class Server {
public:
    Server(uint16_t port, KeyValueStore& kv);
    int run();
private:
    void handleClient(int client_fd);
    void handleExpiration();

    uint16_t server_port_;
    KeyValueStore& kv_;
};


#endif //REDISCLONE_SERVER_H