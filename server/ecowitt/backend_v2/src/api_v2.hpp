#pragma once
#include <microhttpd.h>

namespace api_v2 {
int route(struct MHD_Connection *conn, const char *url, const char *method);
}
