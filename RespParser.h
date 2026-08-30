//
// Created by Daniel Elbaz on 8/29/26.
//

#include <stddef.h>
#include <vector>

#ifndef REDISCLONE_RESPPARSER_H
#define REDISCLONE_RESPPARSER_H



class RespParser {
public:
    void append(char buffer[], size_t len);

    struct Command {
        std::string name;
        std::vector<std::string> args;
    };

    enum class CommandType {
        Ping,
        Set,
        Get,
        Del,
        // Exists,
        // Expire,
        // Ttl,
        Unknown
    };

    CommandType parseCommandType(const std::string& word);

private:
    std::string persistent_buffer_{};

};


#endif //REDISCLONE_RESPPARSER_H