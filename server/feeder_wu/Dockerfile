FROM debian:trixie-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        g++ make libcurl4-openssl-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy Makefile and source tree
COPY Makefile .
COPY src ./src
COPY config.json .

# Build feeder
RUN make

CMD ["./feeder_wu"]
