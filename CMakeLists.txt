CMAKE_MINIMUM_REQUIRED(VERSION 3.15)
PROJECT(THQUIC LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -Wextra")

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_CURRENT_LIST_DIR}/extern/spdlog/install/)

find_package(spdlog REQUIRED)

add_library(lib${PROJECT_NAME} SHARED
    "src/context/quic.cc"
    "src/context/connection_id.cc"

    "src/utils/random.cc"
    "src/utils/socket.cc"
    "src/utils/log.cc"
    "src/utils/variable_codec.cc"
)

target_include_directories(lib${PROJECT_NAME} PUBLIC
    "src/"
)

target_link_libraries(lib${PROJECT_NAME} PUBLIC
    spdlog::spdlog
)

add_executable(${PROJECT_NAME}_server
    "src/server.cc"
)

target_link_libraries(${PROJECT_NAME}_server
    lib${PROJECT_NAME}
)

add_executable(${PROJECT_NAME}_client
    "src/client.cc"
        src/context/common.hh src/context/callback.hh)

target_link_libraries(${PROJECT_NAME}_client
    lib${PROJECT_NAME}
)
