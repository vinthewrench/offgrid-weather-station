#pragma once
#include <microhttpd.h>
#include <thread>
#include <chrono>

namespace http_server {
int start_server(int port);
}
