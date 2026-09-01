//
// Created by Daniel Elbaz on 8/28/26.
//

#include "Server.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

#include "RespParser.h"

Server::Server(uint16_t port, KeyValueStore& kv) : server_port_(port), kv_(kv) {}


int Server::run() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // AF_INET = IPV4 simplicity, stream tcp byte stream, 0 normal protocol

    sockaddr_in address{}; //generates ip address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server_port_);

    bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)); //binds socker to address

    listen(server_fd, 5); // 5 = backlog size

    std::cout << "Listening on port " << server_port_ << "...\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        RespParser parser{}; // one parser per client
        while (true) {
            char buffer[1024]{};
            ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer), 0); //enters the first 1024 bytes into buffer from client
            if (bytes_received > 0) {
                std::cout << "Received: " << buffer << '\n';
                std::cout << "Received " << bytes_received << " bytes\n";
                parser.append(buffer, bytes_received);
                parser.parseAndDispatchCommands([&](const std::string& reply) {
                    send(client_fd, reply.c_str(), std::strlen(reply.c_str()), 0); //send the response
                }, kv_); //lambda that sends the reply to the client
            }
            else if (bytes_received == 0) {
                std::cout << "Client disconnected\n";
                break;
            }
            else {
                perror("recv");
            }

            const char* response = "+PONG\r\n";
        }
        close(client_fd);
    }
    close(server_fd);

    return 0;
}
