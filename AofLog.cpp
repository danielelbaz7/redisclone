#include "AofLog.h"

std::string encodeRespCommand(const std::string& commandType, const std::vector<std::string>& args) {
    std::string encoded = "*" + std::to_string(args.size() + 1) + "\r\n";
    encoded += "$" + std::to_string(commandType.size()) + "\r\n" + commandType + "\r\n";
    for (const auto& arg : args) {
        encoded += "$" + std::to_string(arg.size()) + "\r\n" + arg + "\r\n";
    }
    return encoded;
}

void AofLog::appendToAof(const std::string& commandType, const std::vector<std::string>& args) {
    std::string encoded = encodeRespCommand(commandType, args);

    std::lock_guard<std::mutex> lock(aofMutex);
    aofFile << encoded;
    aofFile.flush();
}
