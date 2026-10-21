#ifndef REDISCLONE_REPLICASET_H
#define REDISCLONE_REPLICASET_H

#include <mutex>
#include <string>
#include <vector>

// the primary's list of connected replica sockets, every write gets sent to all of them
class ReplicaSet {
public:
    void add(int fd);
    void remove(int fd);
    void broadcast(const std::string& encoded);

private:
    std::vector<int> fds_;
    std::mutex mutex_;
};

#endif //REDISCLONE_REPLICASET_H
