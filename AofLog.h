#ifndef REDISCLONE_AOFLOG_H
#define REDISCLONE_AOFLOG_H

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

std::string encodeRespCommand(const std::string& commandType, const std::string& key, const std::optional<std::string>& value);

// decodes back-to-back resp arrays. stops at the first incomplete one (a write cut off by a crash),
// validLength is set to how many bytes were complete commands
std::vector<std::vector<std::string>> decodeRespCommands(const std::string& data, std::size_t& validLength);

class AofLog {
public:
    explicit AofLog(const std::string& path = "appendonly.aof");
    ~AofLog(); //writes whatever is still queued, then stops the writer thread

    void appendToAof(const std::string& commandType, const std::string& key, const std::optional<std::string>& value);
    void appendEncoded(const std::string& encoded); //only queues, the writer thread does the file write

    std::string readAll();

    // every complete command in the file, a partial command at the end gets cut off the file
    std::vector<std::vector<std::string>> loadCommands();

private:
    void writeLoop();

    std::string path_;
    std::ofstream aofFile; //only the writer thread touches it
    std::mutex aofMutex; //guards pending_ and stopping_
    std::condition_variable wakeup_; //signals the writer that there is data or it should stop
    std::string pending_; //commands queued but not written yet
    bool stopping_ = false;
    std::thread writer_; //last, so everything above exists before the thread starts
};

#endif //REDISCLONE_AOFLOG_H
