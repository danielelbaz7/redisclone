#ifndef REDISCLONE_DISPATCHER_H
#define REDISCLONE_DISPATCHER_H

#include "RespParser.h"
#include <string>

#include "KeyValueStore.h"
#include "ReplicaSet.h"

std::string executeCommand(const RespParser::Command& cmd, KeyValueStore &kv, int client_fd = -1, ReplicaSet* replicas = nullptr, bool read_only = false);

#endif //REDISCLONE_DISPATCHER_H
