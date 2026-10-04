#ifndef REDISCLONE_AOFLOG_H
#define REDISCLONE_AOFLOG_H

#include <fstream>
#include <mutex>
#include <optional>
#include <string>

std::string encodeRespCommand(const std::string& commandType, const std::string& key, const std::optional<std::string>& value);

class AofLog {
public:
    void appendToAof(const std::string& commandType, const std::string& key, const std::optional<std::string>& value);

    std::string readAll();

private:
    std::ofstream aofFile{"appendonly.aof", std::ios::app};
    std::mutex aofMutex;
};

#endif //REDISCLONE_AOFLOG_H
