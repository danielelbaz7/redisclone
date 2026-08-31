//
// Created by Daniel Elbaz on 8/29/26.
//

#include "RespParser.h"
#include "Dispatcher.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <iostream>

void RespParser::append(char buffer[], size_t len) {
    persistent_buffer_.append(buffer, len);
}
void RespParser::parseAndDispatchCommands(std::function<void(const std::string&)> onReply) {
    while (true) {
        ParseResult result = parseCommand(); //stores status and command
        if (result.status == ParseStatus::Invalid) {
            std::cout << "Invalid command." << std::endl;
            break;
        }
        if (result.status == ParseStatus::Incomplete) {
            std::cout << "Incomplete command." << std::endl;
            std::cout << persistent_buffer_ << std::endl;
            break;
        }
        if (result.status == ParseStatus::Complete) {
            std::cout << "Complete command." << std::endl;
            std::string commandReply = executeCommand(result.command.value()); //executes command via dispatcher
            onReply(commandReply);
        }
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
            index += 3; // skip the \r\n and the $
            last_parsed = index;
            break;
        }
        index++;
    }


    uint32_t next_word_size = 0;
    std::vector<std::string> words{};

    for (size_t e = 0; e < element_count; e++) {
        while (true) {
            if (persistent_buffer_.substr(index, 2) == "\r\n") { //parse size of next word
                auto start = persistent_buffer_.data() + last_parsed;
                auto end = persistent_buffer_.data() + index;
                std::from_chars(start, end, next_word_size);
                if (persistent_buffer_.substr(index, 2) != "\r\n")
                    return {ParseStatus::Invalid, std::nullopt};
                index += 2;
                last_parsed = index;

                //parse the actual next word
                std::string next_word = persistent_buffer_.substr(last_parsed, next_word_size);
                if (e == 0) {
                    type = parseType(next_word);
                } else {
                    words.push_back(next_word);
                }
                index += next_word_size + (e + 1 < element_count ? 3 : 2); // 3 for every word except last
                last_parsed = index;
                break;
            }

            index++;
        }
    }

    Command command{type, words};
    persistent_buffer_.erase(0, index);
    return {ParseStatus::Complete, command};

}
