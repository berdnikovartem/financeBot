FROM ubuntu:22.04

# Prevent interactive timezone prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# Install C++ build tools and necessary libraries
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libcurl4-openssl-dev \
    libpqxx-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /worker

COPY CMakeLists.txt .
COPY main.cpp .

# Compile the C++ program
RUN cmake . && make

# Start the worker
CMD ["./crypto_worker"]