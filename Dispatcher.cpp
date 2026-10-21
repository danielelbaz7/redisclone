#include "Dispatcher.h"
#include "AofLog.h"
#include <charconv>
#include <functional>
#include <iostream>
#include <mutex>
#include <sys/socket.h>
#include <unordered_map>

namespace {

std::string handlePing(const RespParser::Command& cmd, KeyValueStore &kv) {
    //kv is not needed here but used so map has all the same function signatures
    return "+PONG\r\n";
}

std::string handleSet(const RespParser::Command& cmd, KeyValueStore &kv) {
    if (cmd.args.size() != 2) {
        return "-ERR invalid argument count\r\n";
    }

    kv.set(cmd.args[0], cmd.args[1]);
    return "+OK\r\n";
}

std::string handleGet(const RespParser::Command& cmd, KeyValueStore &kv) {
    if (cmd.args.size() != 1) {
        return "-ERR invalid argument count\r\n";
    }

    std::optional<std::string> value = kv.get(cmd.args[0]);
    if (value.has_value()) { //if a value at that key was found return in resp, size then val
        return "$" + std::to_string(value->size()) +
           "\r\n" +
           *value +
           "\r\n";
    }

    return "$-1\r\n";
}

std::string handleDel(const RespParser::Command& cmd, KeyValueStore &kv) {
    int deleted = 0;
    std::string line = "DEL";
    for (std::string w : cmd.args) {
        if (kv.del(w)) {
            deleted++;
        }
        line += " " + w;
    }
    return ":" + std::to_string(deleted) + "\r\n";
}

std::string handleExpire(const RespParser::Command& cmd, KeyValueStore &kv) {
    if (cmd.args.size() != 2) {
        return "-ERR invalid argument count\r\n";
    }

    int seconds = 0;
    auto [ptr, ec] = std::from_chars(cmd.args[1].data(), cmd.args[1].data() + cmd.args[1].size(), seconds);
    if (ec != std::errc()) {
        return "-ERR value is not an integer or out of range\r\n";
    }

    int result = kv.expire(cmd.args[0], seconds);
    return ":" + std::to_string(result) + "\r\n";
}

std::string handleExpireAt(const RespParser::Command& cmd, KeyValueStore &kv) {
    if (cmd.args.size() != 2) {
        return "-ERR invalid argument count\r\n";
    }

    long long epochSeconds = 0;
    auto [ptr, ec] = std::from_chars(cmd.args[1].data(), cmd.args[1].data() + cmd.args[1].size(), epochSeconds);
    if (ec != std::errc()) {
        return "-ERR value is not an integer or out of range\r\n";
    }

    int result = kv.expireAt(cmd.args[0], epochSeconds);
    return ":" + std::to_string(result) + "\r\n";
}

std::string handleFlushAll(const RespParser::Command& cmd, KeyValueStore &kv) {
    kv.flushAll();
    return "+OK\r\n";
}

std::string handleTtl(const RespParser::Command& cmd, KeyValueStore &kv) {
    if (cmd.args.size() != 1) {
        return "-ERR invalid argument count\r\n";
    }

    std::optional<long long> result = kv.ttl(cmd.args[0]);
    if (!result.has_value()) {
        return ":-2\r\n";
    }
    return ":" + std::to_string(*result) + "\r\n";
}

std::string handleReplSync(KeyValueStore &kv, int client_fd, ReplicaSet* replicas) {
    //a replica is connecting: send +OK and a full copy of the data, then stream every write after it
    if (client_fd < 0 || replicas == nullptr) {
        return "-ERR REPLSYNC needs a connection\r\n";
    }
    //the store stays locked from the snapshot until the replica is registered,
    //so every write is either in the snapshot or streamed after it, never lost or sent twice
    kv.snapshot([&](const std::string& data) {
        std::string reply = "+OK\r\n" + data;
        size_t sent = 0;
        while (sent < reply.size()) { //a big snapshot may take more than one send
            ssize_t n = send(client_fd, reply.data() + sent, reply.size() - sent, 0);
            if (n <= 0) {
                return; //replica went away, don't register it
            }
            sent += n;
        }
        replicas->add(client_fd);
    });
    std::cout << "Replica connected\n";
    return ""; //already replied
}

std::string handleUnimplemented(const RespParser::Command& cmd, KeyValueStore &kv) {
    return "-ERR not implemented\r\n";
}

using Handler = std::function<std::string(const RespParser::Command&, KeyValueStore&)>;

const std::unordered_map<RespParser::CommandType, Handler> kHandlers = {
    {RespParser::CommandType::Ping, handlePing},
    {RespParser::CommandType::Set,  handleSet},
    {RespParser::CommandType::Get,  handleGet},
    {RespParser::CommandType::Del,  handleDel},
{RespParser::CommandType::Expire,  handleExpire},
    {RespParser::CommandType::ExpireAt, handleExpireAt},
    {RespParser::CommandType::Ttl,     handleTtl},
    {RespParser::CommandType::FlushAll, handleFlushAll},
};

bool isWrite(RespParser::CommandType type) { // checks if a command writes to the store
    switch (type) {
        case RespParser::CommandType::Set:
        case RespParser::CommandType::Del:
        case RespParser::CommandType::Expire:
        case RespParser::CommandType::ExpireAt:
        case RespParser::CommandType::FlushAll:
            return true;
        default:
            return false;
    }
}

} // namespace

std::string executeCommand(const RespParser::Command& cmd, KeyValueStore &kv, int client_fd, ReplicaSet* replicas, bool read_only) {
    if (read_only && isWrite(cmd.name)) { //a replica's data only changes through the primary, or the two drift apart
        return "-READONLY You can't write against a read only replica.\r\n";
    }
    if (cmd.name == RespParser::CommandType::ReplSync) { //only command that needs the socket
        return handleReplSync(kv, client_fd, replicas);
    }
    auto it = kHandlers.find(cmd.name);
    if (it == kHandlers.end()) {
        return "-ERR unknown command\r\n";
    }
    return it->second(cmd, kv);
}

//on reply is a function that sends the reply to the client, passed as a lambda in server as of now
bool dispatchAll(RespParser& parser, const std::function<void(const std::string&)>& onReply, KeyValueStore &kv,
                 int client_fd, ReplicaSet* replicas, bool read_only) {
    while (true) {
        RespParser::ParseResult result = parser.next();
        if (result.status == RespParser::ParseStatus::Invalid) {
            std::cout << "Invalid command." << std::endl;
            onReply("-ERR Protocol error\r\n");
            return false; // can't tell where the next command starts, so the connection has to go
        }
        if (result.status == RespParser::ParseStatus::Incomplete) {
            std::cout << "Incomplete command." << std::endl;
            return true;
        }
        std::cout << "Complete command." << std::endl;
        onReply(executeCommand(result.command.value(), kv, client_fd, replicas, read_only));
    }
}
