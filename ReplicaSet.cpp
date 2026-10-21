#include "ReplicaSet.h"

#include <algorithm>
#include <iostream>
#include <sys/socket.h>

#include "KeyValueStore.h"

void ReplicaSet::add(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    fds_.push_back(fd);
}

bool ReplicaSet::syncAndAdd(int fd, KeyValueStore& kv) {
    bool added = false;
    //the store stays locked from the snapshot until the replica is added,
    //so every write is either in the snapshot or streamed after it, never lost or sent twice
    kv.snapshot([&](const std::string& data) {
        std::string reply = "+OK\r\n" + data;
        size_t sent = 0;
        while (sent < reply.size()) { //a big snapshot may take more than one send
            ssize_t n = send(fd, reply.data() + sent, reply.size() - sent, 0);
            if (n <= 0) {
                return; //replica went away, don't add it
            }
            sent += n;
        }
        add(fd);
        added = true;
    });
    return added;
}

void ReplicaSet::remove(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase(fds_, fd);
}

void ReplicaSet::broadcast(const std::string& encoded) {
    // sending under the lock keeps two writes from interleaving their bytes on the same socket
    // this can happen when two client threads are writing into the primary
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = fds_.begin(); it != fds_.end(); ) {
        if (send(*it, encoded.data(), encoded.size(), 0) < 0) {
            std::cout << "Dropping replica, send failed\n";
            it = fds_.erase(it); // its handleClient thread will close the fd when recv fails
        } else {
            ++it;
        }
    }
}
