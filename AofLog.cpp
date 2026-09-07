#include "AofLog.h"

void AofLog::appendToAof(const std::string& line) {
    std::lock_guard<std::mutex> lock(aofMutex);
    aofFile << line << "\n";
    aofFile.flush();
}
