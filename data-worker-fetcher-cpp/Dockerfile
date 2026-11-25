FROM ubuntu:22.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    make \
    libssl-dev \
    libcurl4-openssl-dev \
    libpqxx-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /worker

COPY CMakeLists.txt .
COPY src/ ./src/
RUN cmake . && make

FROM ubuntu:22.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libcurl4 \
    libpqxx-6.4 \
    libssl3 \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 app \
    && useradd --system --uid 10001 --gid app --home-dir /worker app
WORKDIR /worker
COPY --from=build --chown=10001:10001 /worker/crypto_worker ./crypto_worker
USER 10001:10001
ENTRYPOINT ["./crypto_worker"]