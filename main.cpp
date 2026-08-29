#include <iostream>

#include "Server.h"

int main() {
    Server server{};
    server.run(6379);
}
