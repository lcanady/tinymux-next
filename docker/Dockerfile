# =============================================================================
# TinyMUX:Next — Production Server Image
# Docker Hub: lcanady/tinymux-next
#
# A modern rebirth of TinyMUX — WebSocket-native, modular, C++17.
#
# Stage 1: builder  — compiles netmux, shared modules, and test binaries.
# Stage 2: runtime  — minimal production image; test mode available via CMD.
# =============================================================================

# ---- builder ----------------------------------------------------------------
FROM ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libssl-dev \
    libcurl4-openssl-dev \
    catch2 \
    pkg-config \
    autoconf \
    automake \
    libtool \
    libpcre2-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build/mux/src

# Copy the full mux/ source tree (one level up from WORKDIR)
COPY mux/ /build/mux/

# Configure and build netmux (C++17, SSL enabled)
RUN ./configure --enable-ssl
RUN make CXXFLAGS=-std=c++17 depend 2>/dev/null; true
RUN make CXXFLAGS=-std=c++17 -j$(nproc) all

# Build all .so modules
RUN make -C modules CXXFLAGS=-std=c++17 -j$(nproc)

# Build the Catch2 unit-test binary (Release = no ASan, works without libasan at runtime)
RUN cmake -S tests -B tests/build \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_STANDARD=17 && \
    cmake --build tests/build -j$(nproc) && \
    test -f tests/build/ws_tests

# ---- runtime ----------------------------------------------------------------
FROM ubuntu:24.04 AS runtime
LABEL org.opencontainers.image.title="TinyMUX:Next" \
      org.opencontainers.image.description="A modern rebirth of TinyMUX — WebSocket-native, modular, C++17 MUSH server." \
      org.opencontainers.image.version="2.13.0.6" \
      org.opencontainers.image.url="https://hub.docker.com/r/lcanady/tinymux-next" \
      org.opencontainers.image.source="https://github.com/lcanady/tinymux" \
      org.opencontainers.image.licenses="Artistic-2.0"
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libcurl4 \
    libpcre2-8-0 \
    python3 \
    python3-pip \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Game directory layout
RUN mkdir -p /mux/game/bin /mux/game/data /mux/game/scripts /mux/tests

# Copy built artifacts
COPY --from=builder /build/mux/src/netmux           /mux/game/bin/netmux
COPY --from=builder /build/mux/src/libmux.so        /mux/game/bin/libmux.so
COPY --from=builder /build/mux/src/modules/*.so     /mux/game/bin/
COPY --from=builder /build/mux/src/tests/build/ws_tests /mux/tests/ws_tests

# Copy game assets
COPY --from=builder /build/mux/game/data/           /mux/game/data/
COPY --from=builder /build/mux/game/text/           /mux/game/text/

# Copy test configuration and scripts
COPY docker/game/netmux.conf  /mux/game/netmux.conf
COPY docker/game/scripts/     /mux/game/scripts/

# Copy Python test tools
COPY mux/src/tests/mock_httpd.py       /mux/tests/mock_httpd.py
COPY mux/src/tests/mux_test_runner.py  /mux/tests/mux_test_runner.py

# In-game testkit softcode
COPY mux/src/tests/softcode/  /mux/tests/softcode/

COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh /mux/game/bin/netmux /mux/game/scripts/*.sh 2>/dev/null || true

ENV LD_LIBRARY_PATH=/mux/game/bin
WORKDIR /mux/game

# Persistent storage: database files, logs, and user scripts.
# Mount host paths or named volumes here to survive container restarts.
VOLUME ["/mux/game/data", "/mux/game/scripts"]

EXPOSE 4201 4202
ENTRYPOINT ["/entrypoint.sh"]
CMD ["server"]
