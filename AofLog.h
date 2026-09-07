#ifndef REDISCLONE_AOFLOG_H
#define REDISCLONE_AOFLOG_H

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

std::string encodeRespCommand(const std::string& commandType, const std::vector<std::string>& args);

class AofLog {
public:
    void appendToAof(const std::string& commandType, const std::vector<std::string>& args);
private:
    std::ofstream aofFile{"appendonly.aof", std::ios::app};
    std::mutex aofMutex;
};

#endif //REDISCLONE_AOFLOG_H
