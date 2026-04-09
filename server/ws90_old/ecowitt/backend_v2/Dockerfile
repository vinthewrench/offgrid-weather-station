###############################################################
# Stage 1: Build the backend with matching GLIBC environment
###############################################################
FROM debian:12-slim AS builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    g++ make libcurl4-openssl-dev libsqlite3-dev libmicrohttpd-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source
COPY . .

# Build
RUN make clean && make

###############################################################
# Stage 2: Runtime image (tiny)
###############################################################
FROM debian:12-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    libcurl4 libsqlite3-0 libmicrohttpd12 && \
    rm -rf /var/lib/apt/lists/*

ENV TZ=America/Chicago

WORKDIR /app

# Copy binary from builder stage
COPY --from=builder /app/ecowitt_backend_v2 /usr/local/bin/ecowitt_backend_v2

# Copy data/config/state files
COPY config.json /app/config.json
COPY weather_history_v2.sqlite3 /app/weather_history_v2.sqlite3
COPY rain_state_v2.json /state/rain_state_v2.json

EXPOSE 8889

CMD ["/usr/local/bin/ecowitt_backend_v2"]
