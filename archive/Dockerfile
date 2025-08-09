# Stage 1: Builder
FROM ubuntu:22.04 AS builder
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libboost-system-dev \
    libboost-thread-dev \
    nlohmann-json3-dev

# Clone and build websocketpp
RUN git clone https://github.com/zaphoyd/websocketpp.git /websocketpp

WORKDIR /app
COPY . .
RUN mkdir build && cd build && \
    cmake -DCMAKE_PREFIX_PATH=/websocketpp .. && \
    make -j$(nproc)

# Stage 2: Runner
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libssl3 libboost-system1.74.0 libboost-thread1.74.0 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /app/build/hft_engine .
COPY --from=builder /app/config.json .
CMD ["./hft_engine"]