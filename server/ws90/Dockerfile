FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

# Install rtl_433, rtl-sdr tools, and Soapy with RTL-SDR module
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
    rtl-433 \
    rtl-sdr \
    #  soapysdr-tools \
    #   soapysdr0.8-module-rtlsdr \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy your known-good binaries from the host
# (built on the Pi, live in ./bin on the host)
COPY bin/rtl_433 /usr/local/bin/rtl_433
COPY bin/ws90_api /usr/local/bin/ws90_api
RUN chmod +x /usr/local/bin/rtl_433 /usr/local/bin/ws90_api

# Entrypoint that sets up /tmp/ws90.fifo, runs rtl_433 + ws90_api
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

EXPOSE 7890

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
