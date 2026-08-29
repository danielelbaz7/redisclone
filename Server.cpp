//
// Created by Daniel Elbaz on 8/28/26.
//

#include "Server.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

int Server::run(uint16_t port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // AF_INET = IPV4 simplicity, stream tcp byte stream, 0 normal protocol

    sockaddr_in address{}; //generates ip address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)); //binds socker to address

    listen(server_fd, 5); // 5 = backlog size

    std::cout << "Listening on port " << port << "...\n";

    int client_fd = accept(server_fd, nullptr, nullptr);

    char buffer[1024]{};
    recv(client_fd, buffer, sizeof(buffer), 0); //enters the first 1024 bytes into buffer from client

    std::cout << "Received:\n" << buffer << '\n';

    const char* response = "+PONG\r\n";
    send(client_fd, response, std::strlen(response), 0); //send the response

    close(client_fd);
    close(server_fd);

    return 0;
}
