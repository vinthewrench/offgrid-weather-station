#include <cstdio>
#include "state_v2.hpp"
#include "http_server.hpp"

static const int DEFAULT_PORT = 8889;

int main()
{
    std::puts("ecowitt_backend_v2 starting up");

    // Initialize state, DB, poller, etc.
    state_v2::init();

    // Start HTTP server on port DEFAULT_PORT
    int port = DEFAULT_PORT;
    if (http_server::start_server(port) != 0) {
        std::fprintf(stderr, "Failed to start HTTP server on port %d\n", port);
        return 1;
    }

    return 0; // we only get here if server_start returned cleanly
}
