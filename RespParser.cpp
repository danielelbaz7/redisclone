//
// Created by Daniel Elbaz on 8/29/26.
//

#include "RespParser.h"

void RespParser::append(char buffer[], size_t len) {
    persistent_buffer_.append(buffer);
}
