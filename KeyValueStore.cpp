//
// Created by Daniel Elbaz on 8/31/26.
//

#include "KeyValueStore.h"

std::optional<std::string> KeyValueStore::get(std::string key) {
    if (store.contains(key)) {
        return store.at(key);
    }

    return std::nullopt;
}

void KeyValueStore::set(std::string key, std::string value) {
    store[key] = value;
}

size_t KeyValueStore::del(std::string key) {
    return store.erase(key) > 0;
}


