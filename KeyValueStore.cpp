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


