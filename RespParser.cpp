//
// Created by Daniel Elbaz on 8/29/26.
//

#include "RespParser.h"
#include <algorithm>
#include <cctype>

void RespParser::append(char buffer[], size_t len) {
    persistent_buffer_.append(buffer);
}

RespParser::CommandType RespParser::parseCommandType(const std::string& word) {
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                    [](unsigned char c) { return std::toupper(c); });

    if (upper == "PING") return CommandType::Ping;
    if (upper == "SET")  return CommandType::Set;
    if (upper == "GET")  return CommandType::Get;
    if (upper == "DEL")  return CommandType::Del;

    return CommandType::Unknown;
}
