#ifndef REDISCLONE_DISPATCHER_H
#define REDISCLONE_DISPATCHER_H

#include "RespParser.h"
#include <string>

std::string executeCommand(const RespParser::Command& cmd);

#endif //REDISCLONE_DISPATCHER_H
