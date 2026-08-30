//
// Created by Daniel Elbaz on 8/29/26.
//

#include "RespParser.h"
#include <algorithm>
#include <cctype>
#include <charconv>

void RespParser::append(char buffer[], size_t len) {
    persistent_buffer_.append(buffer, len);
    ParseResult command = parseCommand();
    while (command.status == ParseStatus::Complete) {

    }
}

RespParser::CommandType RespParser::parseType(const std::string& word) {
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                    [](unsigned char c) { return std::toupper(c); });

    if (upper == "PING") return CommandType::Ping;
    if (upper == "SET")  return CommandType::Set;
    if (upper == "GET")  return CommandType::Get;
    if (upper == "DEL")  return CommandType::Del;

    return CommandType::Unknown;
}

RespParser::ParseResult RespParser::parseCommand() { //parses from private buffer field, no params needed
    if (persistent_buffer_.size() <= 0) {
        return {ParseStatus::Incomplete, std::nullopt};
    }

    std::size_t index = 0;

    if (persistent_buffer_[index] != '*') {
        return {ParseStatus::Invalid, std::nullopt};
        // will allow inline later, right now its invalid
    }

    index++;


    uint32_t element_count = 0;
    CommandType type{CommandType::Unknown};

    uint32_t last_parsed = index; //using this so we know where the last word starts

    while (index <= persistent_buffer_.size()) { // parse element count
        if (persistent_buffer_.substr(index, index+4) != "\r\n") {
            if (element_count == 0) { // first get number
                auto start = persistent_buffer_.data() + last_parsed;
                auto end = persistent_buffer_.data() + index;
                std::from_chars(start, end, element_count);
                index += 4;
                last_parsed = index;
            } else { // then get command word
                type = parseType(persistent_buffer_.substr(last_parsed, index));
            }
            break;
        }
        index++;
    }



    for (size_t e = 1; e < element_count; e++) {

    }

    return {ParseStatus::Incomplete, std::nullopt};

}
