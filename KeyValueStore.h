//
// Created by Daniel Elbaz on 8/31/26.
//

#ifndef REDISCLONE_KEYVALUESTORE_H
#define REDISCLONE_KEYVALUESTORE_H
#include <unordered_map>
#include <string>
#include <optional>
#include <mutex>
#include <chrono>
#include <vector>
#include <functional>

#include "AofLog.h"

class KeyValueStore {
public:
    explicit KeyValueStore(const std::string &aofPath = "appendonly.aof");
    std::optional<std::string> get(const std::string &key);
    void set(const std::string &key, std::string value);
    int expire(const std::string &key, int seconds);
    std::optional<long long> ttl(const std::string &key);
    int del(const std::string &key);
    void purgeExpired();
    // called with the resp bytes of every write, under the shard lock so order is preserved
    void setOnWrite(std::function<void(const std::string&)> onWrite) { onWrite_ = std::move(onWrite); }
private:
    struct Shard {
        std::unordered_map<std::string, std::string> store;
        std::unordered_map<std::string, std::chrono::time_point<std::chrono::steady_clock>> expirations;
        std::mutex mutex_;
    };

    std::vector<Shard> shards;
    AofLog aof_;
    std::function<void(const std::string&)> onWrite_;

    // every write goes through here: appended to the aof and passed to onWrite_
    void logWrite(const std::string &commandType, const std::string &key, const std::optional<std::string> &value);

    Shard& findShard(const std::string& key) { //will be used in every function, split the kvstore up into shards
        return shards[std::hash<std::string>{}(key) % shards.size()];
    }

    // mutate only, never logs to the AOF — used by set/del AND by replay. caller must hold shard.mutex_
    void applySet(Shard &shard, const std::string &key, const std::string &value);
    int applyDel(Shard &shard, const std::string &key);
    int applyExpireAt(Shard &shard, const std::string &key, long long epochSeconds);

    void replayAof();

};


#endif //REDISCLONE_KEYVALUESTORE_H