// plain test runner, no framework: each test is a function, CHECK records failures and keeps going

#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include "AofLog.h"
#include "Dispatcher.h"
#include "KeyValueStore.h"
#include "ReplicaSet.h"
#include "RespParser.h"

namespace {

int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
        failures++; \
    } \
} while (0)

// fresh aof path per test, deleted afterwards
struct TempAof {
    std::string path;
    TempAof() {
        static int counter = 0;
        path = (std::filesystem::temp_directory_path() /
            ("redisclone_test_" + std::to_string(getpid()) + "_" + std::to_string(counter++) + ".aof")).string();
        std::filesystem::remove(path);
    }
    ~TempAof() { std::filesystem::remove(path); }
};

void appendRaw(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::app | std::ios::binary);
    out << bytes;
}

std::string readRaw(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

RespParser::Command cmd(RespParser::CommandType type, std::vector<std::string> args = {}) {
    return {type, std::move(args)};
}

// feeds raw bytes through a parser and collects every reply
std::vector<std::string> sendRaw(RespParser& parser, KeyValueStore& kv, std::string bytes) {
    std::vector<std::string> replies;
    parser.append(bytes.data(), bytes.size());
    parser.parseAndDispatchCommands([&](const std::string& reply) { replies.push_back(reply); }, kv);
    return replies;
}

// ---------- resp encoding / decoding ----------

void testEncodeSet() {
    CHECK(encodeRespCommand("SET", "k", "v") == "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n");
}

void testEncodeDelHasNoValue() {
    CHECK(encodeRespCommand("DEL", "key", std::nullopt) == "*2\r\n$3\r\nDEL\r\n$3\r\nkey\r\n");
}

void testDecodeRoundTripMultiple() {
    std::string data = encodeRespCommand("SET", "a", "1") + encodeRespCommand("DEL", "a", std::nullopt)
        + encodeRespCommand("EXPIREAT", "b", "123");
    std::size_t valid = 0;
    auto commands = decodeRespCommands(data, valid);
    CHECK(valid == data.size());
    CHECK(commands.size() == 3);
    CHECK((commands[0] == std::vector<std::string>{"SET", "a", "1"}));
    CHECK((commands[1] == std::vector<std::string>{"DEL", "a"}));
    CHECK((commands[2] == std::vector<std::string>{"EXPIREAT", "b", "123"}));
}

void testDecodeValueContainingCrlf() {
    std::string value = "line1\r\nline2\r\n*3\r\n";
    std::string data = encodeRespCommand("SET", "k", value);
    std::size_t valid = 0;
    auto commands = decodeRespCommands(data, valid);
    CHECK(commands.size() == 1);
    CHECK(commands[0][2] == value);
}

void testDecodeEmptyValue() {
    std::size_t valid = 0;
    auto commands = decodeRespCommands(encodeRespCommand("SET", "k", ""), valid);
    CHECK(commands.size() == 1);
    CHECK(commands[0][2].empty());
}

void testDecodeEmptyInput() {
    std::size_t valid = 99;
    CHECK(decodeRespCommands("", valid).empty());
    CHECK(valid == 0);
}

void testDecodeStopsAtTruncatedTail() {
    std::string good = encodeRespCommand("SET", "a", "1");
    std::string full = encodeRespCommand("SET", "b", "hello");
    // every possible cut point of the second command should keep only the first
    for (std::size_t cut = 1; cut < full.size(); cut++) {
        std::size_t valid = 0;
        auto commands = decodeRespCommands(good + full.substr(0, cut), valid);
        CHECK(commands.size() == 1);
        CHECK(valid == good.size());
    }
}

void testDecodeRejectsGarbage() {
    std::size_t valid = 0;
    CHECK(decodeRespCommands("hello world\r\n", valid).empty());
    CHECK(valid == 0);
    CHECK(decodeRespCommands("*2\r\n$abc\r\n", valid).empty());
    CHECK(decodeRespCommands("*1\r\n$3\r\nSETxx", valid).empty()); // missing \r\n after bulk string
}

// ---------- key value store ----------

void testSetGet() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    kv.set("k", "v");
    CHECK(kv.get("k") == "v");
    CHECK(!kv.get("missing").has_value());
}

void testOverwrite() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    kv.set("k", "1");
    kv.set("k", "2");
    CHECK(kv.get("k") == "2");
}

void testDel() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    kv.set("k", "v");
    CHECK(kv.del("k") == 1);
    CHECK(!kv.get("k").has_value());
    CHECK(kv.del("k") == 0);
}

void testDelMissingIsNotLogged() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    kv.del("nope");
    CHECK(readRaw(aof.path).empty());
}

void testTtlStates() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    CHECK(!kv.ttl("k").has_value()); // -2
    kv.set("k", "v");
    CHECK(kv.ttl("k") == -1);
    CHECK(kv.expire("k", 100) == 1);
    long long ttl = kv.ttl("k").value();
    CHECK(ttl >= 99 && ttl <= 100);
}

void testExpireMissingKey() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    CHECK(kv.expire("missing", 10) == 0);
    CHECK(readRaw(aof.path).empty());
}

void testDelClearsExpiration() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    kv.set("k", "v");
    kv.expire("k", 100);
    kv.del("k");
    kv.set("k", "v2");
    CHECK(kv.ttl("k") == -1);
}

void testPurgeExpired() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    kv.set("gone", "v");
    kv.set("stays", "v");
    kv.expire("gone", 0);
    kv.expire("stays", 100);
    kv.purgeExpired();
    CHECK(!kv.get("gone").has_value());
    CHECK(kv.get("stays") == "v");
}

// ---------- crash recovery ----------

void testRecoverSet() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("a", "1"); kv.set("b", "2"); }
    KeyValueStore kv{aof.path};
    CHECK(kv.get("a") == "1");
    CHECK(kv.get("b") == "2");
}

void testRecoverLastWriteWins() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("k", "1"); kv.set("k", "2"); kv.set("k", "3"); }
    KeyValueStore kv{aof.path};
    CHECK(kv.get("k") == "3");
}

void testRecoverDel() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("k", "v"); kv.set("other", "x"); kv.del("k"); }
    KeyValueStore kv{aof.path};
    CHECK(!kv.get("k").has_value());
    CHECK(kv.get("other") == "x");
}

void testRecoverSetAfterDel() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("k", "1"); kv.del("k"); kv.set("k", "2"); }
    KeyValueStore kv{aof.path};
    CHECK(kv.get("k") == "2");
}

void testRecoverTtl() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("k", "v"); kv.expire("k", 100); }
    KeyValueStore kv{aof.path};
    CHECK(kv.get("k") == "v");
    long long ttl = kv.ttl("k").value();
    CHECK(ttl >= 98 && ttl <= 100);
}

void testRecoverNoTtlStaysPersistent() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("k", "v"); }
    KeyValueStore kv{aof.path};
    CHECK(kv.ttl("k") == -1);
}

void testRecoverDropsKeyExpiredWhileDown() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("k", "v"); kv.expire("k", -5); kv.set("live", "v"); }
    KeyValueStore kv{aof.path};
    CHECK(!kv.get("k").has_value()); // gone immediately, not waiting on purge
    CHECK(kv.get("live") == "v");
}

void testRecoverHandWrittenExpireAt() {
    TempAof aof;
    appendRaw(aof.path, encodeRespCommand("SET", "old", "v") + encodeRespCommand("EXPIREAT", "old", "1000")
        + encodeRespCommand("SET", "new", "v") + encodeRespCommand("EXPIREAT", "new", "99999999999"));
    KeyValueStore kv{aof.path};
    CHECK(!kv.get("old").has_value());
    CHECK(kv.get("new") == "v");
    CHECK(kv.ttl("new").value() > 1000);
}

void testRecoverIgnoresBadExpireAt() {
    TempAof aof;
    appendRaw(aof.path, encodeRespCommand("SET", "k", "v") + encodeRespCommand("EXPIREAT", "k", "soon"));
    KeyValueStore kv{aof.path};
    CHECK(kv.get("k") == "v");
    CHECK(kv.ttl("k") == -1);
}

void testRecoverBinarySafeValues() {
    TempAof aof;
    std::string value = "has\r\nnewlines and spaces\r\n$5\r\n";
    std::string key = "key with spaces";
    { KeyValueStore kv{aof.path}; kv.set(key, value); }
    KeyValueStore kv{aof.path};
    CHECK(kv.get(key) == value);
}

void testRecoverTruncatedTail() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("a", "1"); }
    std::size_t goodSize = readRaw(aof.path).size();
    appendRaw(aof.path, "*3\r\n$3\r\nSET\r\n$1\r\nb\r\n$5\r\nhel"); // crash mid write
    {
        KeyValueStore kv{aof.path};
        CHECK(kv.get("a") == "1");
        CHECK(!kv.get("b").has_value());
        CHECK(readRaw(aof.path).size() == goodSize); // partial command cut off
        kv.set("c", "3");
    }
    KeyValueStore kv{aof.path};
    CHECK(kv.get("a") == "1");
    CHECK(kv.get("c") == "3"); // would be lost if it was appended after the garbage
}

void testRecoverMissingFile() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    CHECK(!kv.get("anything").has_value());
}

void testReplayDoesNotRelog() {
    TempAof aof;
    { KeyValueStore kv{aof.path}; kv.set("k", "v"); kv.expire("k", 100); kv.del("k"); }
    std::string before = readRaw(aof.path);
    { KeyValueStore kv{aof.path}; }
    { KeyValueStore kv{aof.path}; }
    CHECK(readRaw(aof.path) == before);
}

void testRecoverManyKeysAcrossShards() {
    TempAof aof;
    {
        KeyValueStore kv{aof.path};
        for (int i = 0; i < 2000; i++) kv.set("key" + std::to_string(i), std::to_string(i));
        for (int i = 0; i < 2000; i += 2) kv.del("key" + std::to_string(i));
    }
    KeyValueStore kv{aof.path};
    int wrong = 0;
    for (int i = 0; i < 2000; i++) {
        auto v = kv.get("key" + std::to_string(i));
        if (i % 2 == 0 ? v.has_value() : v != std::to_string(i)) wrong++;
    }
    CHECK(wrong == 0);
}

void testRecoverConcurrentWriters() {
    TempAof aof;
    const int threads = 8, perThread = 500;
    {
        KeyValueStore kv{aof.path};
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; t++) {
            workers.emplace_back([&, t] {
                for (int i = 0; i < perThread; i++) {
                    std::string key = "t" + std::to_string(t) + "_" + std::to_string(i);
                    kv.set(key, key);
                    if (i % 3 == 0) kv.del(key);
                }
            });
        }
        for (auto& w : workers) w.join();
    }
    std::size_t valid = 0;
    std::string data = readRaw(aof.path);
    decodeRespCommands(data, valid);
    CHECK(valid == data.size()); // no interleaved writes

    KeyValueStore kv{aof.path};
    int wrong = 0;
    for (int t = 0; t < threads; t++) {
        for (int i = 0; i < perThread; i++) {
            std::string key = "t" + std::to_string(t) + "_" + std::to_string(i);
            auto v = kv.get(key);
            if (i % 3 == 0 ? v.has_value() : v != key) wrong++;
        }
    }
    CHECK(wrong == 0);
}

void testConcurrentSetDelSameKeyRecoversToLiveState() {
    // set and del racing on one key: replay has to end in the same state the live store ended in
    for (int round = 0; round < 20; round++) {
        TempAof aof;
        std::optional<std::string> live;
        {
            KeyValueStore kv{aof.path};
            std::atomic<bool> go{false};
            std::thread setter([&] { while (!go) {} for (int i = 0; i < 300; i++) kv.set("k", "v"); });
            std::thread deleter([&] { while (!go) {} for (int i = 0; i < 300; i++) kv.del("k"); });
            go = true;
            setter.join();
            deleter.join();
            live = kv.get("k");
        }
        KeyValueStore kv{aof.path};
        CHECK(kv.get("k") == live);
    }
}

// ---------- dispatcher ----------

void testDispatchCommands() {
    using T = RespParser::CommandType;
    TempAof aof;
    KeyValueStore kv{aof.path};
    CHECK(executeCommand(cmd(T::Ping), kv) == "+PONG\r\n");
    CHECK(executeCommand(cmd(T::Set, {"k", "hello"}), kv) == "+OK\r\n");
    CHECK(executeCommand(cmd(T::Get, {"k"}), kv) == "$5\r\nhello\r\n");
    CHECK(executeCommand(cmd(T::Get, {"missing"}), kv) == "$-1\r\n");
    CHECK(executeCommand(cmd(T::Ttl, {"k"}), kv) == ":-1\r\n");
    CHECK(executeCommand(cmd(T::Ttl, {"missing"}), kv) == ":-2\r\n");
    CHECK(executeCommand(cmd(T::Expire, {"k", "50"}), kv) == ":1\r\n");
    CHECK(executeCommand(cmd(T::Expire, {"missing", "50"}), kv) == ":0\r\n");
    CHECK(executeCommand(cmd(T::Set, {"k2", "x"}), kv) == "+OK\r\n");
    CHECK(executeCommand(cmd(T::Del, {"k", "k2", "missing"}), kv) == ":2\r\n");
}

void testDispatchErrors() {
    using T = RespParser::CommandType;
    TempAof aof;
    KeyValueStore kv{aof.path};
    CHECK(executeCommand(cmd(T::Set, {"only_key"}), kv) == "-ERR invalid argument count\r\n");
    CHECK(executeCommand(cmd(T::Get, {}), kv) == "-ERR invalid argument count\r\n");
    CHECK(executeCommand(cmd(T::Expire, {"k", "abc"}), kv) == "-ERR value is not an integer or out of range\r\n");
    CHECK(executeCommand(cmd(T::Unknown), kv) == "-ERR unknown command\r\n");
}

// ---------- parser ----------

void testParserSingleCommand() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    RespParser parser;
    auto replies = sendRaw(parser, kv, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
    CHECK((replies == std::vector<std::string>{"+OK\r\n"}));
    CHECK(kv.get("foo") == "bar");
}

void testParserPipelined() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    RespParser parser;
    auto replies = sendRaw(parser, kv,
        "*1\r\n$4\r\nPING\r\n*3\r\n$3\r\nSET\r\n$1\r\na\r\n$1\r\n1\r\n*2\r\n$3\r\nGET\r\n$1\r\na\r\n");
    CHECK((replies == std::vector<std::string>{"+PONG\r\n", "+OK\r\n", "$1\r\n1\r\n"}));
}

void testParserCaseInsensitive() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    RespParser parser;
    auto replies = sendRaw(parser, kv, "*1\r\n$4\r\nping\r\n*3\r\n$3\r\nsEt\r\n$1\r\na\r\n$1\r\n1\r\n");
    CHECK((replies == std::vector<std::string>{"+PONG\r\n", "+OK\r\n"}));
}

void testParserCommandSplitAtEveryByte() {
    std::string full = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\nhello\r\n";
    for (std::size_t cut = 1; cut < full.size(); cut++) {
        TempAof aof;
        KeyValueStore kv{aof.path};
        RespParser parser;
        auto first = sendRaw(parser, kv, full.substr(0, cut));
        CHECK(first.empty()); // nothing runs until the whole command is here
        auto second = sendRaw(parser, kv, full.substr(cut));
        CHECK((second == std::vector<std::string>{"+OK\r\n"}));
        CHECK(kv.get("k") == "hello");
    }
}

void testParserPipelinedOneByteAtATime() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    RespParser parser;
    std::string bytes = "*3\r\n$3\r\nSET\r\n$1\r\na\r\n$2\r\n10\r\n*2\r\n$3\r\nGET\r\n$1\r\na\r\n*1\r\n$4\r\nPING\r\n";
    std::vector<std::string> replies;
    for (char c : bytes) {
        for (auto& r : sendRaw(parser, kv, std::string(1, c))) replies.push_back(r);
    }
    CHECK((replies == std::vector<std::string>{"+OK\r\n", "$2\r\n10\r\n", "+PONG\r\n"}));
}

void testParserSplitValueContainingCrlf() {
    std::string value = "a\r\nb";
    std::string full = encodeRespCommand("SET", "k", value);
    for (std::size_t cut = 1; cut < full.size(); cut++) {
        TempAof aof;
        KeyValueStore kv{aof.path};
        RespParser parser;
        sendRaw(parser, kv, full.substr(0, cut));
        sendRaw(parser, kv, full.substr(cut));
        CHECK(kv.get("k") == value);
    }
}

void testParserInvalidCommandAsksToClose() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    RespParser parser;
    std::vector<std::string> replies;
    auto collect = [&](const std::string& r) { replies.push_back(r); };

    std::string good = "*1\r\n$4\r\nPING\r\n*1\r\n$4";
    parser.append(good.data(), good.size());
    CHECK(parser.parseAndDispatchCommands(collect, kv)); // complete + incomplete are both fine

    TempAof aof2;
    KeyValueStore kv2{aof2.path};
    RespParser badParser;
    std::string bad = "hello\r\n";
    badParser.append(bad.data(), bad.size());
    replies.clear();
    CHECK(!badParser.parseAndDispatchCommands(collect, kv2)); // caller should close the connection
    CHECK((replies == std::vector<std::string>{"-ERR Protocol error\r\n"}));
}

void testEndToEndRecoveryThroughParser() {
    TempAof aof;
    {
        KeyValueStore kv{aof.path};
        RespParser parser;
        sendRaw(parser, kv, "*3\r\n$3\r\nSET\r\n$4\r\nuser\r\n$5\r\nalice\r\n"
                            "*3\r\n$6\r\nEXPIRE\r\n$4\r\nuser\r\n$3\r\n500\r\n");
    }
    KeyValueStore kv{aof.path};
    RespParser parser;
    auto replies = sendRaw(parser, kv, "*2\r\n$3\r\nGET\r\n$4\r\nuser\r\n");
    CHECK((replies == std::vector<std::string>{"$5\r\nalice\r\n"}));
    CHECK(kv.ttl("user").value() >= 498);
}

// ---------- replication ----------

// a connected pair of sockets: [0] plays the primary's end, [1] plays the replica's end
struct FakeReplica {
    int fds[2]{-1, -1};
    FakeReplica() { socketpair(AF_UNIX, SOCK_STREAM, 0, fds); }
    ~FakeReplica() { close(fds[0]); if (fds[1] >= 0) close(fds[1]); }

    std::string readAvailable() { // everything sent so far, without blocking
        std::string out;
        char buffer[4096];
        ssize_t n;
        while ((n = recv(fds[1], buffer, sizeof(buffer), MSG_DONTWAIT)) > 0) out.append(buffer, n);
        return out;
    }
};

// connects a replica list to the store the same way Server does
void attach(KeyValueStore& kv, ReplicaSet& replicas) {
    kv.setOnWrite([&](const std::string& encoded) { replicas.broadcast(encoded); });
}

void testReplicaGetsEveryWrite() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    ReplicaSet replicas;
    attach(kv, replicas);
    FakeReplica replica;
    replicas.add(replica.fds[0]);
    kv.set("k", "v");
    kv.expire("k", 100);
    kv.del("k");
    std::size_t valid = 0;
    auto commands = decodeRespCommands(replica.readAvailable(), valid);
    CHECK(commands.size() == 3);
    CHECK((commands[0] == std::vector<std::string>{"SET", "k", "v"}));
    CHECK(commands[1][0] == "EXPIREAT");
    CHECK((commands[2] == std::vector<std::string>{"DEL", "k"}));
}

void testReplicaStreamMatchesAof() {
    TempAof aof;
    FakeReplica replica;
    {
        KeyValueStore kv{aof.path};
        ReplicaSet replicas;
        attach(kv, replicas);
        replicas.add(replica.fds[0]);
        kv.set("a", "1");
        kv.set("b", "2");
        kv.del("a");
        kv.del("missing"); // not a write, sent nowhere
    }
    CHECK(replica.readAvailable() == readRaw(aof.path));
}

void testReadsAreNotSentToReplica() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    kv.set("k", "v");
    ReplicaSet replicas;
    attach(kv, replicas);
    FakeReplica replica;
    replicas.add(replica.fds[0]);
    kv.get("k");
    kv.ttl("k");
    kv.expire("missing", 10);
    CHECK(replica.readAvailable().empty());
}

void testRemovedReplicaGetsNothing() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    ReplicaSet replicas;
    attach(kv, replicas);
    FakeReplica replica;
    replicas.add(replica.fds[0]);
    replicas.remove(replica.fds[0]);
    kv.set("k", "v");
    CHECK(replica.readAvailable().empty());
}

void testDeadReplicaIsDroppedAndOthersStillGetWrites() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    ReplicaSet replicas;
    attach(kv, replicas);
    FakeReplica dead, alive;
    replicas.add(dead.fds[0]);
    replicas.add(alive.fds[0]);
    close(dead.fds[1]); // replica went away
    dead.fds[1] = -1;
    kv.set("a", "1"); // must not crash (SIGPIPE) and must still reach the live one
    kv.set("b", "2");
    std::size_t valid = 0;
    CHECK(decodeRespCommands(alive.readAvailable(), valid).size() == 2);
    CHECK(kv.get("b") == "2");
}

void testReplSyncRegistersConnection() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    ReplicaSet replicas;
    attach(kv, replicas);
    FakeReplica replica;
    RespParser parser;
    std::string bytes = "*1\r\n$8\r\nREPLSYNC\r\n";
    std::vector<std::string> replies;
    parser.append(bytes.data(), bytes.size());
    parser.parseAndDispatchCommands([&](const std::string& r) { replies.push_back(r); }, kv, replica.fds[0], &replicas);
    kv.set("k", "v");
    CHECK(replica.readAvailable() == "+OK\r\n" + encodeRespCommand("SET", "k", "v")); // +OK first, then the stream
}

void testNoOnWriteStillWorks() {
    TempAof aof;
    KeyValueStore kv{aof.path}; // nothing attached, writes only go to the aof
    kv.set("k", "v");
    kv.del("k");
    CHECK(!kv.get("k").has_value());
}

void testReplSyncWithoutConnectionErrors() {
    TempAof aof;
    KeyValueStore kv{aof.path};
    CHECK(executeCommand(cmd(RespParser::CommandType::ReplSync), kv) == "-ERR REPLSYNC needs a connection\r\n");
}

struct TestCase {
    const char* name;
    void (*fn)();
};

const TestCase kTests[] = {
    {"encode set", testEncodeSet},
    {"encode del has no value", testEncodeDelHasNoValue},
    {"decode round trip multiple", testDecodeRoundTripMultiple},
    {"decode value containing crlf", testDecodeValueContainingCrlf},
    {"decode empty value", testDecodeEmptyValue},
    {"decode empty input", testDecodeEmptyInput},
    {"decode stops at truncated tail", testDecodeStopsAtTruncatedTail},
    {"decode rejects garbage", testDecodeRejectsGarbage},
    {"set get", testSetGet},
    {"overwrite", testOverwrite},
    {"del", testDel},
    {"del missing is not logged", testDelMissingIsNotLogged},
    {"ttl states", testTtlStates},
    {"expire missing key", testExpireMissingKey},
    {"del clears expiration", testDelClearsExpiration},
    {"purge expired", testPurgeExpired},
    {"recover set", testRecoverSet},
    {"recover last write wins", testRecoverLastWriteWins},
    {"recover del", testRecoverDel},
    {"recover set after del", testRecoverSetAfterDel},
    {"recover ttl", testRecoverTtl},
    {"recover no ttl stays persistent", testRecoverNoTtlStaysPersistent},
    {"recover drops key expired while down", testRecoverDropsKeyExpiredWhileDown},
    {"recover hand written expireat", testRecoverHandWrittenExpireAt},
    {"recover ignores bad expireat", testRecoverIgnoresBadExpireAt},
    {"recover binary safe values", testRecoverBinarySafeValues},
    {"recover truncated tail", testRecoverTruncatedTail},
    {"recover missing file", testRecoverMissingFile},
    {"replay does not relog", testReplayDoesNotRelog},
    {"recover many keys across shards", testRecoverManyKeysAcrossShards},
    {"recover concurrent writers", testRecoverConcurrentWriters},
    {"concurrent set/del same key recovers to live state", testConcurrentSetDelSameKeyRecoversToLiveState},
    {"dispatch commands", testDispatchCommands},
    {"dispatch errors", testDispatchErrors},
    {"parser single command", testParserSingleCommand},
    {"parser pipelined", testParserPipelined},
    {"parser case insensitive", testParserCaseInsensitive},
    {"parser command split at every byte", testParserCommandSplitAtEveryByte},
    {"parser pipelined one byte at a time", testParserPipelinedOneByteAtATime},
    {"parser split value containing crlf", testParserSplitValueContainingCrlf},
    {"parser invalid command asks to close", testParserInvalidCommandAsksToClose},
    {"end to end recovery through parser", testEndToEndRecoveryThroughParser},
    {"replica gets every write", testReplicaGetsEveryWrite},
    {"replica stream matches aof", testReplicaStreamMatchesAof},
    {"reads are not sent to replica", testReadsAreNotSentToReplica},
    {"removed replica gets nothing", testRemovedReplicaGetsNothing},
    {"dead replica is dropped and others still get writes", testDeadReplicaIsDroppedAndOthersStillGetWrites},
    {"replsync registers connection", testReplSyncRegistersConnection},
    {"no onWrite still works", testNoOnWriteStillWorks},
    {"replsync without connection errors", testReplSyncWithoutConnectionErrors},
};

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN); // same as the server, so a dead replica test doesn't kill the run
    int failedTests = 0;
    for (const TestCase& test : kTests) {
        int before = failures;
        test.fn();
        bool passed = failures == before;
        if (!passed) failedTests++;
        std::cerr << (passed ? "PASS " : "FAIL ") << test.name << "\n";
    }
    std::cerr << "\n" << (std::size(kTests) - failedTests) << "/" << std::size(kTests) << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
