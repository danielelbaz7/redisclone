//
// Created by Daniel Elbaz on 8/31/26.
//

#include "KeyValueStore.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace {
const std::string kFlushAllCommand = "*1\r\n$8\r\nFLUSHALL\r\n";
}

KeyValueStore::KeyValueStore(const std::string &aofPath) : shards(16), aof_(aofPath) {
    replayAof();
}


void KeyValueStore::logWrite(const std::string &commandType, const std::string &key, const std::optional<std::string> &value) {
    logEncoded(encodeRespCommand(commandType, key, value));
}

void KeyValueStore::logEncoded(const std::string &encoded) {
    aof_.appendEncoded(encoded);
    if (onWrite_) { //empty when no server is attached, e.g. in tests
        onWrite_(encoded);
    }
}

std::vector<std::unique_lock<std::mutex>> KeyValueStore::lockAllShards() {
    std::vector<std::unique_lock<std::mutex>> locks;
    for (Shard& shard : shards) {
        locks.emplace_back(shard.mutex_);
    }
    return locks; //all unlock when the vector is destroyed
}

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
    logWrite("SET", key, value);
    applySet(shard, key, value);
}


int KeyValueStore::applyDel(Shard &shard, const std::string &key) { //exists for reading from aof
    int result = shard.store.erase(key);
    if (result > 0) {
        shard.expirations.erase(key);
        return true;
    }
    return false;
}

int KeyValueStore::del(const std::string &key) {
    Shard& shard = findShard(key);
    std::lock_guard<std::mutex> lock(shard.mutex_); // log under the lock so aof order matches apply order
    int result = applyDel(shard, key);
    if (result > 0) {
        logWrite("DEL", key, std::nullopt);
        return 1;
    }
    return 0;
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
    logWrite("EXPIREAT", key, std::to_string(epochSeconds));

    shard.expirations[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return 1;
}

int KeyValueStore::expireAt(const std::string &key, long long epochSeconds) { //what the replica gets from the primary
    Shard& shard = findShard(key);
    std::lock_guard<std::mutex> lock(shard.mutex_);
    if (!shard.store.contains(key)) {
        return 0;
    }
    logWrite("EXPIREAT", key, std::to_string(epochSeconds));
    return applyExpireAt(shard, key, epochSeconds);
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

void KeyValueStore::applySet(Shard &shard, const std::string &key, const std::string &value) {
    shard.store[key] = value;
}

int KeyValueStore::applyExpireAt(Shard &shard, const std::string &key, long long epochSeconds) {
    if (!shard.store.contains(key)) {
        return 0;
    }

    // aof stores wall clock time, convert it back to a steady_clock deadline
    long long remaining = epochSeconds - std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (remaining <= 0) {
        applyDel(shard, key); // expired while the server was down
        return 1;
    }

    // cap at what a live EXPIRE (int seconds) could produce so a huge timestamp can't overflow steady_clock
    remaining = std::min(remaining, static_cast<long long>(std::numeric_limits<int>::max()));
    shard.expirations[key] = std::chrono::steady_clock::now() + std::chrono::seconds(remaining);
    return 1;
}

void KeyValueStore::replayAof() { // rebuilds the store on startup, nothing gets logged again
    for (const std::vector<std::string>& args : aof_.loadCommands()) {
        if (args.size() == 1 && args[0] == "FLUSHALL") {
            auto locks = lockAllShards();
            applyFlushAll();
            continue;
        }
        if (args.size() < 2) {
            continue;
        }
        const std::string& type = args[0];
        const std::string& key = args[1];
        Shard& shard = findShard(key);
        std::lock_guard<std::mutex> lock(shard.mutex_);

        if (type == "SET" && args.size() == 3) {
            applySet(shard, key, args[2]);
        } else if (type == "DEL") {
            applyDel(shard, key);
        } else if (type == "EXPIREAT" && args.size() == 3) {
            long long epochSeconds = 0;
            auto [ptr, ec] = std::from_chars(args[2].data(), args[2].data() + args[2].size(), epochSeconds);
            if (ec == std::errc()) {
                applyExpireAt(shard, key, epochSeconds);
            }
        }
    }
}

void KeyValueStore::applyFlushAll() {
    for (Shard& shard : shards) {
        shard.store.clear();
        shard.expirations.clear();
    }
}

void KeyValueStore::flushAll() {
    auto locks = lockAllShards();
    logEncoded(kFlushAllCommand);
    applyFlushAll();
}

void KeyValueStore::snapshot(const std::function<void(const std::string&)>& afterSnapshot) {
    auto locks = lockAllShards();
    auto steadyNow = std::chrono::steady_clock::now();
    auto systemNow = std::chrono::system_clock::now();

    std::string data = kFlushAllCommand; //wipe whatever the receiver had first
    for (Shard& shard : shards) {
        for (const auto& [key, value] : shard.store) {
            auto it = shard.expirations.find(key);
            if (it != shard.expirations.end() && it->second <= steadyNow) {
                continue; //already expired, purge just hasn't removed it yet
            }
            data += encodeRespCommand("SET", key, value);
            if (it != shard.expirations.end()) { //steady_clock deadline -> wall clock timestamp, same as the aof uses
                auto deadline = systemNow + std::chrono::duration_cast<std::chrono::system_clock::duration>(it->second - steadyNow);
                long long epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(deadline.time_since_epoch()).count();
                data += encodeRespCommand("EXPIREAT", key, std::to_string(epochSeconds));
            }
        }
    }
    afterSnapshot(data); //still holding every lock
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



