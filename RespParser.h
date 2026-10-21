//
// Created by Daniel Elbaz on 8/29/26.
//

#include <functional>
#include <stddef.h>
#include <vector>
#include <optional>
#include <string>

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
        Expire,
        ExpireAt,
        Ttl,
        ReplSync,
        FlushAll,
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

    // takes the next complete command off the buffer. Incomplete = wait for more bytes,
    // Invalid = protocol error, the buffer is dropped since the stream can't be trusted anymore
    ParseResult next();

private:
    std::string persistent_buffer_{};
    ParseResult parseCommand();

};


#endif //REDISCLONE_RESPPARSER_H