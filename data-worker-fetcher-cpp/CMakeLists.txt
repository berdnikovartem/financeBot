cmake_minimum_required(VERSION 3.16)
project(crypto_worker)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenSSL REQUIRED)
find_package(libpqxx QUIET)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)

add_executable(crypto_worker
        src/main.cpp
        src/ConfigFetcher.cpp
        src/MarketAnalyzer.cpp
        src/Bitunix.cpp
        src/Bitunix.h
)

target_link_libraries(crypto_worker
        PRIVATE
        pqxx
        nlohmann_json::nlohmann_json
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
)