#include <iostream>

#include "Server.h"
#include "KeyValueStore.h"

int main() {
    KeyValueStore store{};
    Server server{6379, store};
    server.run();
}
