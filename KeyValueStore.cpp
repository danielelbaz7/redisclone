//
// Created by Daniel Elbaz on 8/31/26.
//

#include "KeyValueStore.h"

std::optional<std::string> KeyValueStore::get(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (store.contains(key)) {
        return store.at(key);
    }

    return std::nullopt;
}

void KeyValueStore::set(const std::string &key, std::string value) {
    std::lock_guard<std::mutex> lock(mutex_);
    store[key] = value;
}

int KeyValueStore::del(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store.erase(key) > 0;
}

int KeyValueStore::expire(const std::string &key, int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool key_in_store = store.contains(key);

    if (!key_in_store) {
        return 0;
    }

    std::lock_guard<std::mutex> lockExpiry(expire_mutex_);
    expirations[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    return 1;
}

std::optional<long long> KeyValueStore::ttl(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!store.contains(key)) {
        return std::nullopt; // key doesn't exist -> caller replies -2
    }

    std::lock_guard<std::mutex> lockExpiry(expire_mutex_);
    auto it = expirations.find(key);
    if (it == expirations.end()) {
        return -1; // key exists but has no expiry set
    }

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        it->second - std::chrono::steady_clock::now()).count();
    return remaining > 0 ? remaining : 0;
}

void KeyValueStore::purgeExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::lock_guard<std::mutex> lockExpiry(expire_mutex_);

    auto now = std::chrono::steady_clock::now();
    for (auto it = expirations.begin(); it != expirations.end(); ) {
        if (it->second <= now) {
            store.erase(it->first);
            it = expirations.erase(it); // erase() returns the next valid iterator
        } else {
            ++it;
        }
    }
}



