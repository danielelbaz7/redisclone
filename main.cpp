#include <iostream>

#include "Server.h"

int main() {
    Server server{6379};
    server.run();
}
