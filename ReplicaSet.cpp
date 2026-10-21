#include "ReplicaSet.h"

#include <algorithm>
#include <iostream>
#include <sys/socket.h>

void ReplicaSet::add(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    fds_.push_back(fd);
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
