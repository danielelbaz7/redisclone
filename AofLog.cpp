#include "AofLog.h"

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

void AofLog::appendToAof(const std::string& commandType, const std::string& key, const std::optional<std::string>& value) {
    std::string encoded = encodeRespCommand(commandType, key, value);

    std::lock_guard<std::mutex> lock(aofMutex);
    aofFile << encoded;
    aofFile.flush();
}
