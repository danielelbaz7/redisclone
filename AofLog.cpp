#include "AofLog.h"

#include <charconv>
#include <filesystem>
#include <sstream>

AofLog::AofLog(const std::string& path)
    : path_(path), aofFile(path, std::ios::app), writer_(&AofLog::writeLoop, this) {}

AofLog::~AofLog() {
    {
        std::lock_guard<std::mutex> lock(aofMutex);
        stopping_ = true;
    }
    wakeup_.notify_one();
    writer_.join(); //writeLoop writes what's left before it returns
}

std::string encodeRespCommand(const std::string& commandType, const std::string& key, const std::optional<std::string>& value) {
    int argCount = value.has_value() ? 3 : 2;
    std::string encoded = "*" + std::to_string(argCount) + "\r\n";
    encoded += "$" + std::to_string(commandType.size()) + "\r\n" + commandType + "\r\n";
    encoded += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
    if (value.has_value()) {
        encoded += "$" + std::to_string(value->size()) + "\r\n" + *value + "\r\n";
    }
    return encoded;
}

std::vector<std::vector<std::string>> decodeRespCommands(const std::string& data, std::size_t& validLength) {
    std::vector<std::vector<std::string>> commands;
    std::size_t index = 0;
    validLength = 0;

    // reads "<prefix><number>\r\n" at index and moves index past it
    auto readNumber = [&](char prefix, long long& out) {
        if (index >= data.size() || data[index] != prefix) return false;
        std::size_t lineEnd = data.find("\r\n", index);
        if (lineEnd == std::string::npos) return false;
        auto [ptr, ec] = std::from_chars(data.data() + index + 1, data.data() + lineEnd, out);
        if (ec != std::errc() || ptr != data.data() + lineEnd) return false;
        index = lineEnd + 2;
        return true;
    };

    while (index < data.size()) {
        long long argCount = 0;
        if (!readNumber('*', argCount) || argCount < 0) break;

        std::vector<std::string> args;
        bool complete = true;
        for (long long i = 0; i < argCount; i++) {
            long long len = 0;
            if (!readNumber('$', len) || len < 0 || index + len + 2 > data.size()
                || data.compare(index + len, 2, "\r\n") != 0) {
                complete = false;
                break;
            }
            args.push_back(data.substr(index, len)); // length prefixed, so values can contain \r\n
            index += len + 2;
        }
        if (!complete) break;

        commands.push_back(std::move(args));
        validLength = index;
    }
    return commands;
}

void AofLog::appendToAof(const std::string& commandType, const std::string& key, const std::optional<std::string>& value) {
    appendEncoded(encodeRespCommand(commandType, key, value));
}

void AofLog::appendEncoded(const std::string& encoded) {
    std::lock_guard<std::mutex> lock(aofMutex);
    pending_ += encoded; //in memory only, the client doesn't wait for the file
    wakeup_.notify_one();
}

void AofLog::writeLoop() { //runs on its own thread
    std::unique_lock<std::mutex> lock(aofMutex);
    while (true) {
        wakeup_.wait(lock, [&] { return !pending_.empty() || stopping_; }); //sleeps until there is work
        if (pending_.empty()) {
            return; //stopping and everything is written
        }
        std::string data;
        data.swap(pending_); //take everything queued so far, one write for many commands
        lock.unlock(); //appendEncoded can keep queueing while we write
        aofFile << data;
        aofFile.flush();
        lock.lock();
    }
}

std::string AofLog::readAll() {
    std::ifstream in(path_);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();

}

std::vector<std::vector<std::string>> AofLog::loadCommands() {
    std::string data = readAll();
    std::size_t validLength = 0;
    std::vector<std::vector<std::string>> commands = decodeRespCommands(data, validLength);
    if (validLength < data.size()) {
        // otherwise new appends land after the garbage and become unreadable on the next restart
        std::lock_guard<std::mutex> lock(aofMutex);
        std::filesystem::resize_file(path_, validLength);
    }
    return commands;
}
