#ifndef REDISCLONE_AOFLOG_H
#define REDISCLONE_AOFLOG_H

#include <fstream>
#include <mutex>
#include <string>

class AofLog {
public:
    void appendToAof(const std::string& line);
private:
    std::ofstream aofFile{"appendonly.aof", std::ios::app};
    std::mutex aofMutex;
};

#endif //REDISCLONE_AOFLOG_H
