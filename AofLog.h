#ifndef REDISCLONE_AOFLOG_H
#define REDISCLONE_AOFLOG_H

#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

std::string encodeRespCommand(const std::string& commandType, const std::string& key, const std::optional<std::string>& value);

// decodes back-to-back resp arrays. stops at the first incomplete one (a write cut off by a crash),
// validLength is set to how many bytes were complete commands
std::vector<std::vector<std::string>> decodeRespCommands(const std::string& data, std::size_t& validLength);

class AofLog {
public:
    explicit AofLog(const std::string& path = "appendonly.aof");

    void appendToAof(const std::string& commandType, const std::string& key, const std::optional<std::string>& value);
    void appendEncoded(const std::string& encoded);

    std::string readAll();

    // every complete command in the file, a partial command at the end gets cut off the file
    std::vector<std::vector<std::string>> loadCommands();

private:
    std::string path_;
    std::ofstream aofFile;
    std::mutex aofMutex;
};

#endif //REDISCLONE_AOFLOG_H
