//
// Created by Daniel Elbaz on 8/29/26.
//

#include <stddef.h>
#include <vector>
#include <optional>

#ifndef REDISCLONE_RESPPARSER_H
#define REDISCLONE_RESPPARSER_H



class RespParser {
public:
    void append(char buffer[], size_t len);

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

    struct Command {
        CommandType name;
        std::vector<std::string> args;
    };

    enum class ParseStatus {
        Complete,
        Incomplete,
        Invalid
    };

    struct ParseResult {
        ParseStatus status;
        std::optional<Command> command;
    };

    CommandType parseType(const std::string& word);


private:
    std::string persistent_buffer_{};
    ParseResult parseCommand();

};


#endif //REDISCLONE_RESPPARSER_H