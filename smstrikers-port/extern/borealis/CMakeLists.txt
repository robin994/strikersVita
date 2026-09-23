# smstrikers-port: only the Discord client and the log it writes to.
find_package(Threads REQUIRED)

# smstrikers-port: app_info.hpp includes version.h for a user agent the Discord client never sends.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/borealis/version.h"
     CONTENT "#pragma once\n#define BOREALIS_APP_DESCRIBE \"\"\n")

add_library(borealis_core INTERFACE)
target_include_directories(borealis_core INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(borealis_core INTERFACE cxx_std_20)

add_library(borealis_log STATIC src/log.cpp)
target_link_libraries(borealis_log PUBLIC borealis_core fmt::fmt aurora::core)

include(FetchContent)
FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(borealis_discord STATIC src/discord/discord.cpp src/discord/discord_protocol.cpp)
add_library(borealis::discord ALIAS borealis_discord)
target_link_libraries(borealis_discord
        PUBLIC borealis_log
        PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
