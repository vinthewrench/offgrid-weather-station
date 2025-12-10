/*
    WS90 Weather Station JSON API Bridge (C++17 edition)
    ----------------------------------------------------

    Author: ChatGPT (OpenAI) — adapted in collaboration with vinthewrench
    License: MIT License

    This version uses nlohmann::json for safe, correct JSON parsing.

    Features:
        * Reads FIFO /tmp/ws90.fifo from rtl_433
        * Handles partial JSON fragments
        * Extracts **complete JSON objects** safely
        * Optionally filters by --id <station_id>
        * Provides small REST HTTP server on port 7890
        * Structured JSON error responses
        * CORS support
        * Detects stale data
        * MIT open source

    Compile:
        g++ -O2 -Wall -Wextra -std=c++17 \
            -o ws90_api ws90_api.cpp

    FIFO setup:
        rtl_433 \
            -d serial=WS90 \
            -f 433920000 \
            -M time:iso \
            -F json:/tmp/ws90.fifo

    Run:
        ./ws90_api                (promiscuous mode)
        ./ws90_api --id 52127     (filter WS90 device)
*/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdarg>

#include <string>
#include <vector>
#include <optional>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "json.hpp"       // nlohmann::json

using json = nlohmann::json;

#define FIFO_PATH "/tmp/ws90.fifo"
#define HTTP_PORT 7890
#define STALE_SECONDS 30
#define MAX_FIFO_CHUNK 2048
#define MAX_JSON_SIZE 8192

static std::string latest_json;
static bool have_json = false;
static time_t last_update = 0;

static std::optional<int> filter_id;

// ---------------------------------------------------------
// Safe socket writer
// ---------------------------------------------------------
static void sock_printf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vdprintf(fd, fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------
// Unified JSON response
// ---------------------------------------------------------
static void send_json_response(int client, int code, const char *reason, const std::string &body) {
    sock_printf(client,
        "HTTP/1.1 %d %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n\r\n"
        "%s\n",
        code, reason, body.c_str());
}

// ---------------------------------------------------------
// FIFO setup
// ---------------------------------------------------------
static int setup_fifo() {
    if (mkfifo(FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        exit(1);
    }

    // Open read end (non-blocking)
    int fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open fifo read");
        exit(1);
    }

    // Open write end to keep FIFO from blocking
    static int keepalive = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (keepalive < 0) {
        // Not fatal — but warn
        perror("open fifo write");
    }

    return fd;
}

// ---------------------------------------------------------
// JSON extraction from FIFO stream
//
// rtl_433 may send partial objects or more than one.
// We scan for '{' … '}' balanced objects and parse them.
// ---------------------------------------------------------
static void process_fifo_bytes(const char *data, ssize_t len) {
    static std::string buf;

    buf.append(data, len);
    if (buf.size() > MAX_JSON_SIZE * 4) {
        buf.erase(0, buf.size() - MAX_JSON_SIZE);
    }

    size_t start = 0;
    while (true) {
        size_t open = buf.find('{', start);
        if (open == std::string::npos)
            break;

        int depth = 0;
        bool valid = false;

        for (size_t i = open; i < buf.size(); i++) {
            if (buf[i] == '{') depth++;
            if (buf[i] == '}') depth--;

            if (depth == 0) {
                size_t end = i + 1;

                std::string obj = buf.substr(open, end - open);

                try {
                    json j = json::parse(obj);

                    // Check for WS90 model
                    if (!j.contains("model"))
                        continue;

                    if (j["model"] != "Fineoffset-WS90")
                        continue;

                    // If ID filtering enabled
                    if (filter_id.has_value()) {
                        if (!j.contains("id"))
                            continue;
                        if (!j["id"].is_number_integer())
                            continue;
                        if ((int)j["id"] != *filter_id)
                            continue;
                    }

                    latest_json = obj;
                    have_json = true;
                    last_update = time(nullptr);

                } catch (...) {
                    // Ignore malformed JSON
                }

                start = end;
                valid = true;
                break;
            }
        }

        if (!valid)
            break; // Await more data
    }

    // Trim buffer to avoid infinite growth
    if (start > 0 && start < buf.size()) {
        buf.erase(0, start);
    }
}

// ---------------------------------------------------------
static int read_fifo(int fd) {
    char tmp[MAX_FIFO_CHUNK];
    ssize_t n = read(fd, tmp, sizeof(tmp));
    if (n > 0) {
        process_fifo_bytes(tmp, n);
    }
    return n;
}

// ---------------------------------------------------------
static bool data_is_stale() {
    return (!have_json || time(nullptr) - last_update > STALE_SECONDS);
}

// ---------------------------------------------------------
// Handle HTTP request
// ---------------------------------------------------------
static void handle_http(int client) {
    char req[512] = {0};
    read(client, req, sizeof(req) - 1);

    char method[8], path[256];
    if (sscanf(req, "%7s %255s", method, path) != 2) {
        send_json_response(client, 400, "Bad Request",
            "{\"error\":\"bad_request\",\"message\":\"Unable to parse request\"}");
        return;
    }

    if (strcmp(method, "GET") != 0) {
        send_json_response(client, 405, "Method Not Allowed",
            "{\"error\":\"method_not_allowed\",\"message\":\"Only GET is supported\"}");
        return;
    }

    if (!(strcmp(path, "/") == 0 || strcmp(path, "/ws90") == 0)) {
        send_json_response(client, 404, "Not Found",
            "{\"error\":\"not_found\",\"message\":\"Unknown endpoint\"}");
        return;
    }

    if (!have_json) {
        send_json_response(client, 503, "Service Unavailable",
            "{\"error\":\"no_data\",\"message\":\"WS90 data not yet available\"}");
        return;
    }

    if (data_is_stale()) {
        send_json_response(client, 503, "Service Unavailable",
            "{\"error\":\"stale_data\",\"message\":\"WS90 data is stale\"}");
        return;
    }

    send_json_response(client, 200, "OK", latest_json);
}

// ---------------------------------------------------------
// Usage helper
// ---------------------------------------------------------
static void print_usage(const char *prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s                 # promiscuous WS90 mode\n"
        "  %s --id <station>  # filter for specific WS90 id\n",
        prog, prog);
}

// ---------------------------------------------------------
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    // --- argument parsing ---
    if (argc == 1) {
        // promiscuous mode, no filter_id
    } else if (argc == 3 && std::strcmp(argv[1], "--id") == 0) {
        char *end = nullptr;
        long val = std::strtol(argv[2], &end, 10);
        if (*end != '\0' || val <= 0 || val > INT32_MAX) {
            std::fprintf(stderr, "Invalid station id: %s\n", argv[2]);
            print_usage(argv[0]);
            return 1;
        }
        filter_id = (int)val;
        std::cout << "Filtering WS90 ID = " << *filter_id << "\n";
    } else {
        print_usage(argv[0]);
        return 1;
    }

    int fifo_fd = setup_fifo();

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(server, 8) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "WS90 API running on port " << HTTP_PORT
              << " FIFO=" << FIFO_PATH << "\n";

    while (1) {
        // Read FIFO
        int n = read_fifo(fifo_fd);
        if (n == 0) {
            close(fifo_fd);
            fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
        }

        // HTTP
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server, &fds);

        timeval tv = {0, 200000};
        int r = select(server + 1, &fds, nullptr, nullptr, &tv);
        if (r > 0 && FD_ISSET(server, &fds)) {
            int client = accept(server, nullptr, nullptr);
            if (client >= 0) {
                handle_http(client);
                close(client);
            }
        }
    }

    return 0;
}
