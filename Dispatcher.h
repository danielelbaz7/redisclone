#ifndef REDISCLONE_DISPATCHER_H
#define REDISCLONE_DISPATCHER_H

#include "RespParser.h"
#include <functional>
#include <string>

#include "KeyValueStore.h"
#include "ReplicaSet.h"

std::string executeCommand(const RespParser::Command& cmd, KeyValueStore &kv, int client_fd = -1, ReplicaSet* replicas = nullptr, bool read_only = false);

// runs every complete command in the parser and passes each reply to onReply.
// returns false on a protocol error, the caller should close the connection since the stream can't be trusted anymore
bool dispatchAll(RespParser& parser, const std::function<void(const std::string&)>& onReply, KeyValueStore &kv,
                 int client_fd = -1, ReplicaSet* replicas = nullptr, bool read_only = false);

#endif //REDISCLONE_DISPATCHER_H
