//
// Created by Daniel Elbaz on 8/28/26.
//

#include "Server.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "RespParser.h"

Server::Server(uint16_t port, KeyValueStore& kv, std::optional<ReplicaOf> primary)
    : server_port_(port), kv_(kv), primary_(std::move(primary)) {
    kv_.setOnWrite([this](const std::string& encoded) { replicas_.broadcast(encoded); }); //set before any client threads start
}

void Server::handleClient(int client_fd) {
    RespParser parser{}; // one parser per client
    while (true) {
        char buffer[1024]{};
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer), 0); //enters the first 1024 bytes into buffer from client
        if (bytes_received > 0) {
            std::cout << "Received: " << buffer << '\n';
            std::cout << "Received " << bytes_received << " bytes\n";
            parser.append(buffer, bytes_received);
            bool stream_ok = parser.parseAndDispatchCommands([&](const std::string& reply) {
                send(client_fd, reply.c_str(), std::strlen(reply.c_str()), 0); //send the response
            }, kv_, client_fd, &replicas_, primary_.has_value()); //replicas are read only for clients
            if (!stream_ok) {
                std::cout << "Protocol error, closing client\n";
                break;
            }
        }
        else if (bytes_received == 0) {
            std::cout << "Client disconnected\n";
            break;
        }
        else {
            perror("recv");
            break;
        }
    }
    replicas_.remove(client_fd); //no-op for normal clients. must happen before close or a new client could reuse the fd
    close(client_fd);
}

void Server::handleExpiration() { //thread function
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        kv_.purgeExpired();
    }
}

void Server::connectToPrimary() { //replica side, we act as a client of the primary
    int primary_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(primary_->port);
    if (inet_pton(AF_INET, primary_->host.c_str(), &address.sin_addr) != 1) { //"127.0.0.1" -> 4 bytes
        std::cerr << "Invalid primary address " << primary_->host << '\n';
        close(primary_fd);
        return;
    }

    if (connect(primary_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        perror("connect");
        close(primary_fd);
        return;
    }
    std::cout << "Connected to primary " << primary_->host << ":" << primary_->port << '\n';

    std::string handshake = "*1\r\n$8\r\nREPLSYNC\r\n"; //tells the primary this connection is a replica
    send(primary_fd, handshake.data(), handshake.size(), 0);

    //the primary answers +OK, then streams writes. read until the +OK line is here
    std::string received;
    while (received.find("\r\n") == std::string::npos) {
        char buffer[1024];
        ssize_t bytes_received = recv(primary_fd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cout << "Lost connection to primary\n";
            close(primary_fd);
            return;
        }
        received.append(buffer, bytes_received);
    }
    if (received.rfind("+OK\r\n", 0) != 0) { //starts with +OK
        std::cerr << "Primary rejected REPLSYNC: " << received << '\n';
        close(primary_fd);
        return;
    }
    std::cout << "Replicating from primary\n";

    RespParser parser{};
    auto ignoreReply = [](const std::string&) {}; //never answer the primary, it isn't waiting for replies
    std::string rest = received.substr(5); //writes that came in the same recv as +OK
    parser.append(rest.data(), rest.size());
    bool stream_ok = parser.parseAndDispatchCommands(ignoreReply, kv_);

    while (stream_ok) { //apply every write the primary sends to our own store
        char buffer[1024];
        ssize_t bytes_received = recv(primary_fd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            std::cout << "Lost connection to primary\n";
            break;
        }
        parser.append(buffer, bytes_received);
        stream_ok = parser.parseAndDispatchCommands(ignoreReply, kv_);
    }
    if (!stream_ok) {
        std::cerr << "Bad data from primary, stopping replication\n";
    }
    close(primary_fd);
}



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

    std::thread(&Server::handleExpiration, this).detach();
    if (primary_.has_value()) {
        std::thread(&Server::connectToPrimary, this).detach();
    }

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        std::thread(&Server::handleClient, this, client_fd).detach();
    }
    close(server_fd);

    return 0;
}
