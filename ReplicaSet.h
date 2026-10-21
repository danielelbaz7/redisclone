#ifndef REDISCLONE_REPLICASET_H
#define REDISCLONE_REPLICASET_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class KeyValueStore;

// the primary's list of connected replicas, every write gets queued for all of them.
// each replica has its own sender thread, so a slow replica never makes a client's write wait
class ReplicaSet {
public:
    // a replica that falls this far behind gets dropped instead of using unlimited memory
    explicit ReplicaSet(size_t max_queued_bytes = 256 * 1024 * 1024);
    ~ReplicaSet(); //sends what is still queued, then stops every sender thread

    void add(int fd, std::string initial = ""); //initial is queued before any write
    // queues +OK and a snapshot of kv for a replica that just sent REPLSYNC, then adds it
    void syncAndAdd(int fd, KeyValueStore& kv);
    void remove(int fd); //sends what is still queued, then stops its sender thread
    void broadcast(const std::string& encoded);

private:
    struct Replica {
        int fd;
        std::mutex mutex; //guards pending, stopping and dead
        std::condition_variable wakeup; //signals the sender that there is data or it should stop
        std::string pending; //bytes queued but not sent yet
        bool stopping = false;
        bool dead = false; //send failed or it fell too far behind, nothing more gets queued
        std::thread sender;
    };

    static void sendLoop(Replica& replica);
    static void stop(Replica& replica);

    size_t max_queued_bytes_;
    std::vector<std::unique_ptr<Replica>> replicas_; //unique_ptr so a Replica never moves while its thread uses it
    std::mutex mutex_; //guards replicas_
};

#endif //REDISCLONE_REPLICASET_H
