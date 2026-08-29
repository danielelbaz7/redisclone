//
// Created by Daniel Elbaz on 8/28/26.
//

#ifndef REDISCLONE_SERVER_H
#define REDISCLONE_SERVER_H
#include <cstdint>


class Server {
public:
    Server(uint16_t port);
    int run();
private:
    uint16_t server_port;
};


#endif //REDISCLONE_SERVER_H