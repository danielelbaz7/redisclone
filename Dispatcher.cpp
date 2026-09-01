#include "Dispatcher.h"
#include <functional>
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
    for (std::string w : cmd.args) {
        if (kv.del(w)) {
            deleted++;
        }
    }
    return ":" + std::to_string(deleted) + "\r\n";
}


std::string handleUnimplemented(const RespParser::Command& cmd, KeyValueStore &kv) {
    return "-ERR not implemented\r\n";
}

using Handler = std::function<std::string(const RespParser::Command&, KeyValueStore&)>;

const std::unordered_map<RespParser::CommandType, Handler> kHandlers = {
    {RespParser::CommandType::Ping, handlePing},
    {RespParser::CommandType::Set,  handleSet},
    {RespParser::CommandType::Get,  handleGet},
    {RespParser::CommandType::Del,  handleUnimplemented},
};

} // namespace

std::string executeCommand(const RespParser::Command& cmd, KeyValueStore &kv) {
    auto it = kHandlers.find(cmd.name);
    if (it == kHandlers.end()) {
        return "-ERR unknown command\r\n";
    }
    return it->second(cmd);
}
