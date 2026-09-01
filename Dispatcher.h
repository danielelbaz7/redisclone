#ifndef REDISCLONE_DISPATCHER_H
#define REDISCLONE_DISPATCHER_H

#include "RespParser.h"
#include <string>

#include "KeyValueStore.h"

std::string executeCommand(const RespParser::Command& cmd, KeyValueStore &kv);

#endif //REDISCLONE_DISPATCHER_H
