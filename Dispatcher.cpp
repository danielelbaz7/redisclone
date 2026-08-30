#include "Dispatcher.h"
#include <functional>
#include <unordered_map>

namespace {

std::string handlePing(const RespParser::Command& cmd) {
    return "+PONG\r\n";
}

std::string handleUnimplemented(const RespParser::Command& cmd) {
    return "-ERR not implemented\r\n";
}

using Handler = std::function<std::string(const RespParser::Command&)>;

const std::unordered_map<RespParser::CommandType, Handler> kHandlers = {
    {RespParser::CommandType::Ping, handlePing},
    {RespParser::CommandType::Set,  handleUnimplemented},
    {RespParser::CommandType::Get,  handleUnimplemented},
    {RespParser::CommandType::Del,  handleUnimplemented},
};

} // namespace

std::string executeCommand(const RespParser::Command& cmd) {
    auto it = kHandlers.find(cmd.name);
    if (it == kHandlers.end()) {
        return "-ERR unknown command\r\n";
    }
    return it->second(cmd);
}
