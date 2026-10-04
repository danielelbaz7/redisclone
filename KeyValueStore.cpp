//
// Created by Daniel Elbaz on 8/31/26.
//

#include "KeyValueStore.h"

KeyValueStore::KeyValueStore() : shards(16) {};


std::optional<std::string> KeyValueStore::get(const std::string &key) {
    Shard& shard = findShard(key);
    std::lock_guard<std::mutex> lock(shard.mutex_);
    if (shard.store.contains(key)) {
        return shard.store.at(key);
    }

    return std::nullopt;
}

void KeyValueStore::set(const std::string &key, std::string value) {
    Shard& shard = findShard(key);
    std::lock_guard<std::mutex> lock(shard.mutex_);
    aof_.appendToAof("SET", key, value);
    shard.store[key] = value;
}

int KeyValueStore::del(const std::string &key) {
    Shard& shard = findShard(key);
    std::lock_guard<std::mutex> lock(shard.mutex_);
    int result = shard.store.erase(key);
    if (result > 0) {
        aof_.appendToAof("DEL", key, std::nullopt);
        shard.expirations.erase(key);
        return true;
    }
    return false;
}

int KeyValueStore::expire(const std::string &key, int seconds) {
    Shard& shard = findShard(key);
    std::lock_guard<std::mutex> lock(shard.mutex_);
    bool key_in_store = shard.store.contains(key);

    if (!key_in_store) {
        return 0;
    }

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(seconds);
    long long epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        deadline.time_since_epoch()).count();
    aof_.appendToAof("EXPIREAT", key, std::to_string(epochSeconds));

    shard.expirations[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return 1;
}

std::optional<long long> KeyValueStore::ttl(const std::string &key) {
    Shard& shard = findShard(key);
    std::lock_guard<std::mutex> lock(shard.mutex_);
    if (!shard.store.contains(key)) {
        return std::nullopt; // key doesn't exist -> caller replies -2
    }

    auto it = shard.expirations.find(key);
    if (it == shard.expirations.end()) {
        return -1; // key exists but has no expiry set
    }

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        it->second - std::chrono::steady_clock::now()).count();
    return remaining > 0 ? remaining : 0;
}

void KeyValueStore::applySet(const std::string &key, const std::string &value) {

}

int KeyValueStore::applyDel(const std::string &key) {

}

int KeyValueStore::applyExpire(const std::string &key, int seconds) {

}

void KeyValueStore::purgeExpired() {
    auto now = std::chrono::steady_clock::now();
    for (Shard& shard : shards) {
        std::lock_guard<std::mutex> lock(shard.mutex_);
        for (auto it = shard.expirations.begin(); it != shard.expirations.end(); ) {
            if (it->second <= now) {
                shard.store.erase(it->first);
                it = shard.expirations.erase(it); // erase() returns the next valid iterator
            } else {
                ++it;
            }
        }
    }
}



