//
// Created by Daniel Elbaz on 8/29/26.
//

#include <functional>
#include <stddef.h>
#include <vector>
#include <optional>

#ifndef REDISCLONE_RESPPARSER_H
#define REDISCLONE_RESPPARSER_H

class KeyValueStore;
class ReplicaSet;

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

    // returns false on a protocol error, the caller should close the connection since the stream can't be trusted anymore
    bool parseAndDispatchCommands(std::function<void(const std::string&)> onReply, KeyValueStore &kv, int client_fd = -1, ReplicaSet* replicas = nullptr, bool read_only = false);

private:
    std::string persistent_buffer_{};
    ParseResult parseCommand();

};


#endif //REDISCLONE_RESPPARSER_H