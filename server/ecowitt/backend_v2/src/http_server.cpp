#include "http_server.hpp"
#include "api_v2.hpp"
#include <microhttpd.h>
#include <cstring>
#include <iostream>

static MHD_Result handle_request(void *cls,
                                 struct MHD_Connection *conn,
                                 const char *url,
                                 const char *method,
                                 const char *version,
                                 const char *upload_data,
                                 size_t *upload_data_size,
                                 void **con_cls)
{
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;

    int r = api_v2::route(conn, url, method);

    // MHD_queue_response returns MHD_YES or MHD_NO as int
    // api_v2::route just forwards that. Normalize to MHD_Result.
    if (r == MHD_YES) {
        return MHD_YES;
    } else {
        return MHD_NO;
    }
}

int http_server::start_server(int port) {
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        port,
        nullptr,
        nullptr,
        &handle_request,
        nullptr,
        MHD_OPTION_END
    );

    if (!daemon) {
        std::cerr << "Failed to start HTTP server on port " << port << std::endl;
        return 1;
    }

    std::cout << "HTTP server running on port " << port << std::endl;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(600));
    }

    MHD_stop_daemon(daemon);
    return 0;
}
