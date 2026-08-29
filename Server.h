//
// Created by Daniel Elbaz on 8/28/26.
//

#ifndef REDISCLONE_SERVER_H
#define REDISCLONE_SERVER_H
#include <cstdint>


class Server {
public:
    int run(uint16_t port);
};


#endif //REDISCLONE_SERVER_H