//
// Created by Daniel Elbaz on 8/31/26.
//

#ifndef REDISCLONE_KEYVALUESTORE_H
#define REDISCLONE_KEYVALUESTORE_H
#include <unordered_map>
#include <string>


class KeyValueStore {
public:
    std::optional<std::string> get(std::string key);
    void set(std::string key, std::string value);
    size_t del(std::string key);
private:
    std::unordered_map<std::string, std::string> store;

};


#endif //REDISCLONE_KEYVALUESTORE_H