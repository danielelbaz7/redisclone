#include "ReplicaSet.h"

#include <algorithm>
#include <iostream>
#include <sys/socket.h>

#include "KeyValueStore.h"

namespace {
bool sendAll(int fd, const std::string& data) { //send() may only take part of the data, keep going until all of it is out
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += n;
    }
    return true;
}
}

ReplicaSet::ReplicaSet(size_t max_queued_bytes) : max_queued_bytes_(max_queued_bytes) {}

ReplicaSet::~ReplicaSet() {
    std::vector<std::unique_ptr<Replica>> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        all.swap(replicas_);
    }
    for (auto& replica : all) {
        stop(*replica);
    }
}

void ReplicaSet::add(int fd, std::string initial) {
    auto replica = std::make_unique<Replica>();
    replica->fd = fd;
    replica->pending = std::move(initial);
    replica->sender = std::thread(&ReplicaSet::sendLoop, std::ref(*replica));
    std::lock_guard<std::mutex> lock(mutex_);
    replicas_.push_back(std::move(replica));
}

void ReplicaSet::syncAndAdd(int fd, KeyValueStore& kv) {
    //the store stays locked from the snapshot until the replica is added,
    //so every write is either in the snapshot or queued after it, never lost or sent twice.
    //the snapshot is only queued here, the sender thread does the slow network part after the locks are released
    kv.snapshot([&](const std::string& data) {
        add(fd, "+OK\r\n" + data);
    });
}

void ReplicaSet::remove(int fd) {
    std::unique_ptr<Replica> removed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(replicas_.begin(), replicas_.end(),
                               [&](const auto& replica) { return replica->fd == fd; });
        if (it == replicas_.end()) {
            return; //normal clients are never in the list
        }
        removed = std::move(*it);
        replicas_.erase(it);
    }
    stop(*removed); //outside mutex_ so broadcast isn't blocked while the last bytes go out
}

void ReplicaSet::broadcast(const std::string& encoded) {
    // the lock makes every replica get writes in the same order, even with many client threads writing.
    // it only covers appending to memory, never a network send
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& replica : replicas_) {
        std::lock_guard<std::mutex> replica_lock(replica->mutex);
        if (replica->dead) {
            continue;
        }
        if (replica->pending.size() + encoded.size() > max_queued_bytes_) {
            std::cout << "Dropping replica, it fell too far behind\n";
            replica->dead = true;
            replica->pending.clear();
            shutdown(replica->fd, SHUT_RDWR); //its handleClient recv returns 0, which removes it
            continue;
        }
        replica->pending += encoded;
        replica->wakeup.notify_one();
    }
}

void ReplicaSet::sendLoop(Replica& replica) { //runs on the replica's own thread
    std::unique_lock<std::mutex> lock(replica.mutex);
    while (true) {
        replica.wakeup.wait(lock, [&] { return !replica.pending.empty() || replica.stopping; }); //sleeps until there is work
        if (replica.pending.empty()) {
            return; //stopping and everything is sent
        }
        std::string data;
        data.swap(replica.pending); //take everything queued so far, one send for many writes
        lock.unlock(); //broadcast can keep queueing while we are on the network
        bool ok = sendAll(replica.fd, data);
        lock.lock();
        if (!ok) {
            std::cout << "Dropping replica, send failed\n";
            replica.dead = true;
            replica.pending.clear();
            shutdown(replica.fd, SHUT_RDWR); //wakes its handleClient so it gets removed and closed
            return;
        }
    }
}

void ReplicaSet::stop(Replica& replica) {
    {
        std::lock_guard<std::mutex> lock(replica.mutex);
        replica.stopping = true;
    }
    replica.wakeup.notify_one();
    replica.sender.join(); //the fd must stay open until the thread is done with it
}
