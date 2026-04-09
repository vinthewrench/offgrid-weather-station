#include "api_v2.hpp"
#include "state_v2.hpp"
#include <microhttpd.h>
#include <string>
#include <cstring>
#include <cstdlib>   // std::strtol
#include <strings.h> // strcasecmp

// ----------------- paging helpers -----------------

// Reasonable defaults; tweak if you want
static constexpr int DEFAULT_DAYS   = 30;    // 0 means "no time filter"
static constexpr int DEFAULT_LIMIT  = 100;
static constexpr int DEFAULT_OFFSET = 0;
static constexpr int MAX_LIMIT      = 365;

// Parse integer query parameter with clamping (case-sensitive name)
[[maybe_unused]]
static int get_query_int(MHD_Connection *conn,
                         const char *name,
                         int default_value,
                         int min_value,
                         int max_value)
{
    const char *val = MHD_lookup_connection_value(conn,
                                                  MHD_GET_ARGUMENT_KIND,
                                                  name);
    if (!val || !*val)
        return default_value;

    char *end = nullptr;
    long v = std::strtol(val, &end, 10);
    if (end == val) {
        // not a number
        return default_value;
    }

    if (v < min_value) v = min_value;
    if (v > max_value) v = max_value;
    return static_cast<int>(v);
}

// ----------------- case-insensitive query helpers -----------------

struct QueryCIContext {
    const char *target;
    const char *value;
};

static MHD_Result query_arg_ci_iter(void *cls,
                                    enum MHD_ValueKind kind,
                                    const char *key,
                                    const char *val)
{
    (void)kind; // unused

    QueryCIContext *ctx = static_cast<QueryCIContext *>(cls);
    if (!key || !val) {
        return MHD_YES;
    }

    // Case-insensitive compare of the query key
    if (strcasecmp(key, ctx->target) == 0) {
        ctx->value = val;
        // Stop iterating; we found our value
        return MHD_NO;
    }

    return MHD_YES;
}

// Return the value of a query parameter, case-insensitive name match.
// Returns nullptr if not present.
static const char *get_query_value_ci(MHD_Connection *conn, const char *name)
{
    QueryCIContext ctx{ name, nullptr };
    MHD_get_connection_values(conn,
                              MHD_GET_ARGUMENT_KIND,
                              query_arg_ci_iter,
                              &ctx);
    return ctx.value;
}

// Parse an integer query parameter, case-insensitive name match.
// Returns default_value if missing or invalid, clamps to [min_value, max_value].
static int get_query_int_ci(MHD_Connection *conn,
                            const char *name,
                            int default_value,
                            int min_value,
                            int max_value)
{
    const char *val = get_query_value_ci(conn, name);
    if (!val || !*val)
        return default_value;

    char *end = nullptr;
    long v = std::strtol(val, &end, 10);
    if (end == val) {
        // not a number
        return default_value;
    }

    if (v < min_value) v = min_value;
    if (v > max_value) v = max_value;
    return static_cast<int>(v);
}

// ----------------- reply_json -----------------

// Send a JSON response with full CORS headers
static MHD_Result reply_json(struct MHD_Connection *conn,
                             const std::string &json,
                             unsigned int status = MHD_HTTP_OK)
{
    struct MHD_Response *res = MHD_create_response_from_buffer(
        json.size(),
        (void *)json.c_str(),
        MHD_RESPMEM_MUST_COPY
    );
    if (!res) return MHD_NO;

    // Required headers for browsers
    MHD_add_response_header(res, "Content-Type", "application/json");
    MHD_add_response_header(res, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(res, "Access-Control-Allow-Methods", "GET, OPTIONS");
    MHD_add_response_header(res, "Access-Control-Allow-Headers", "Content-Type");

    int q = MHD_queue_response(conn, status, res);
    MHD_destroy_response(res);

    return (q == MHD_YES) ? MHD_YES : MHD_NO;
}

// ----------------- api_v2::route with paging -----------------

int api_v2::route(MHD_Connection *conn,
                  const char *url,
                  const char *method)
{
    // Browser preflight: return empty response with CORS headers only
    if (std::strcmp(method, "OPTIONS") == 0) {
        return reply_json(conn, "", MHD_HTTP_NO_CONTENT);
    }

    if (std::strcmp(method, "GET") != 0) {
        return reply_json(conn,
                          "{\"error\":\"method not allowed\"}",
                          MHD_HTTP_METHOD_NOT_ALLOWED);
    }

    // Case-insensitive detection of options
    const char *days_raw   = get_query_value_ci(conn, "days");
    const char *limit_raw  = get_query_value_ci(conn, "limit");
    const char *offset_raw = get_query_value_ci(conn, "offset");

    bool has_days   = (days_raw   && *days_raw);
    bool has_limit  = (limit_raw  && *limit_raw);
    bool has_offset = (offset_raw && *offset_raw);

    // Sentinels:
    //  - days=limit=offset = -1  => simple SELECT (no filter, no limit)
    //  - days>=0, limit=offset=-1 => time filter only, no limit/offset
    //  - anything with limit/offset set => time + paging
    int days   = -1;
    int limit  = -1;
    int offset = -1;

    if (!has_days && !has_limit && !has_offset) {
        // No options at all -> simple SELECT
        days = limit = offset = -1;

    } else if (has_days && !has_limit && !has_offset) {
        // days only -> time filter only, no limit/offset
        days   = get_query_int_ci(conn, "days", 0, 0, 3650);  // 0 = no time filter
        limit  = -1;
        offset = -1;

    } else {
        // Any limit/offset present -> paged mode
        days   = get_query_int_ci(conn, "days",   0,              0, 3650);     // 0 = no time filter
        limit  = get_query_int_ci(conn, "limit",  DEFAULT_LIMIT,  1, MAX_LIMIT);
        offset = get_query_int_ci(conn, "offset", DEFAULT_OFFSET, 0, 1000000);
    }

    if (std::strcmp(url, "/api/v2/weather") == 0) {
        return reply_json(conn, state_v2::current_weather_json());

    } else if (std::strcmp(url, "/api/v2/history/temperature") == 0) {
        return reply_json(conn,
                          state_v2::history_temperature_json(days, limit, offset));

    } else if (std::strcmp(url, "/api/v2/history/humidity") == 0) {
        return reply_json(conn,
                          state_v2::history_humidity_json(days, limit, offset));

    } else if (std::strcmp(url, "/api/v2/history/rain") == 0) {
        return reply_json(conn,
                          state_v2::history_rain_json(days, limit, offset));
    }

    return reply_json(conn,
                      "{\"error\":\"unknown endpoint\"}",
                      MHD_HTTP_NOT_FOUND);
}
