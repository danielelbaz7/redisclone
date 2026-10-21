#ifndef REDISCLONE_REPLICASET_H
#define REDISCLONE_REPLICASET_H

#include <mutex>
#include <string>
#include <vector>

class KeyValueStore;

// the primary's list of connected replica sockets, every write gets sent to all of them
class ReplicaSet {
public:
    void add(int fd);
    // sends +OK and a snapshot of kv to a replica that just sent REPLSYNC, then adds it.
    // returns false if the replica went away during the send
    bool syncAndAdd(int fd, KeyValueStore& kv);
    void remove(int fd);
    void broadcast(const std::string& encoded);

private:
    std::vector<int> fds_;
    std::mutex mutex_;
};

#endif //REDISCLONE_REPLICASET_H
