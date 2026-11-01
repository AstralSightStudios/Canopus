// Windows command-line client for Mirage's real AstroBox C ABI.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include "mirage_astrobox.h"

static std::mutex mutex;
static std::condition_variable completed;
static uint64_t done_id;
static bool done_ok;
static const auto started = std::chrono::steady_clock::now();

static double elapsed() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

static void on_message(const char *json, void *) {
    std::printf("%8.3f %s\n", elapsed(), json);
    std::fflush(stdout);
    if (!std::strstr(json, "\"type\":\"result\"")) return;
    const char *id = std::strstr(json, "\"id\":");
    if (!id) return;
    std::lock_guard<std::mutex> lock(mutex);
    done_id = std::strtoull(id + 5, nullptr, 10);
    done_ok = std::strstr(json, "\"ok\":true") != nullptr;
    completed.notify_one();
}

static bool call(const std::string &method, const std::string &params = "{}") {
    // Connection parameters include a binding key; keep them out of logs.
    std::printf("%8.3f >>> %s\n", elapsed(), method.c_str());
    std::fflush(stdout);
    uint64_t id = mab_call(method.c_str(), params.c_str());
    if (!id) return false;
    std::unique_lock<std::mutex> lock(mutex);
    if (!completed.wait_for(lock, std::chrono::seconds(360), [id] { return done_id == id; })) {
        std::fprintf(stderr, "request timed out: %s\n", method.c_str());
        return false;
    }
    return done_ok;
}

static std::string quoted(const std::string &value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        result += c;
    }
    return result + "\"";
}

static bool request(const std::string &line) {
    auto eq = line.find('=');
    auto method = line.substr(0, eq);
    auto params = eq == std::string::npos ? "{}" : line.substr(eq + 1);
    if (method == "sleep") {
        std::this_thread::sleep_for(std::chrono::seconds(std::stoi(params)));
        return true;
    }
    return call(method, params);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: smoke <port> <authKey> <stateDir> [method[=paramsJSON] ...]\n");
        return 2;
    }
    mab_init(on_message, nullptr);
    std::string params = "{\"host\":\"127.0.0.1\",\"port\":" + std::string(argv[1])
        + ",\"authKey\":" + quoted(argv[2]) + ",\"stateDir\":" + quoted(argv[3])
        + ",\"timeoutSecs\":180";
    if (const char *band = std::getenv("MAB_BAND")) params += ",\"bandAddr\":" + quoted(band);
    params += "}";
    call("status");
    if (!call("connect", params)) return 1;
    bool ok = true;
    if (argc > 4 && std::strcmp(argv[4], "--stdin") == 0) {
        std::printf("%8.3f {\"type\":\"ready\"}\n", elapsed());
        std::fflush(stdout);
        for (std::string line; std::getline(std::cin, line) && line != "quit";) {
            if (!line.empty()) ok = request(line) && ok;
        }
    } else if (argc > 4) {
        for (int i = 4; i < argc; ++i) ok = request(argv[i]) && ok;
    } else {
        for (const char *method : {"deviceInfo", "watchfaces", "quickApps"}) ok = call(method) && ok;
    }
    ok = call("disconnect") && ok;
    call("status");
    return ok ? 0 : 1;
}
