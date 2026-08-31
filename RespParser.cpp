//
// Created by Daniel Elbaz on 8/29/26.
//

#include "RespParser.h"
#include "Dispatcher.h"
#include <algorithm>
#include <cctype>
#include <charconv>

void RespParser::append(char buffer[], size_t len) {
    persistent_buffer_.append(buffer, len);
}
void RespParser::parseAndExecuteCommands(std::function<void(const std::string&)> onReply) {
    while (true) {
        ParseResult result = parseCommand(); //stores status and command
        std::string commandReply = executeCommand(result.command.value()); //executes command via dispatcher
        onReply(commandReply);
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
        if (persistent_buffer_.substr(index, 2) == "\r\n") {
            auto start = persistent_buffer_.data() + last_parsed;
            auto end = persistent_buffer_.data() + index;
            std::from_chars(start, end, element_count);
            if (persistent_buffer_.substr(index, 2) != "\r\n") {
                return {ParseStatus::Invalid, std::nullopt};
            }
            index += 2;
            last_parsed = index;
            break;
        }
        index++;
    }


    uint32_t next_word_size = 0;
    std::vector<std::string> words{};

    for (size_t e = 0; e < element_count; e++) {
        if (e % 2 != 0) {
            if (persistent_buffer_.substr(index, 2) == "\r\n") { //parse size of next word
                auto start = persistent_buffer_.data() + last_parsed;
                auto end = persistent_buffer_.data() + index;
                std::from_chars(start, end, next_word_size);
                if (persistent_buffer_.substr(index, 2) != "\r\n")
                    return {ParseStatus::Invalid, std::nullopt};
                index += 2;
                last_parsed = index;
            } else {
                index++;
            }
        } else {
            std::string next_word = persistent_buffer_.substr(last_parsed, last_parsed + next_word_size);
            words.push_back(next_word);
            index += next_word_size;
            last_parsed = index;
        }
    }

    Command command{type, words};
    persistent_buffer_.erase(0, index);
    return {ParseStatus::Complete, command};

}
