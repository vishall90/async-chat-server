
cmake_minimum_required(VERSION 3.20)
project(async_chat_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use vcpkg manifests
# Expect CMAKE_TOOLCHAIN_FILE pointing to vcpkg toolchain

find_package(asio CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_executable(async_chat_server
  src/main.cpp
  src/chat_server.hpp
  src/utils.hpp
  src/persistence.hpp
)

target_link_libraries(async_chat_server PRIVATE asio::asio nlohmann_json::nlohmann_json spdlog::spdlog)
if(UNIX)
  target_link_libraries(async_chat_server PRIVATE pthread)
endif()

add_executable(chat_cli
  client/cli.cpp
  src/utils.hpp
)

target_link_libraries(chat_cli PRIVATE asio::asio nlohmann_json::nlohmann_json spdlog::spdlog)
if(UNIX)
  target_link_libraries(chat_cli PRIVATE pthread)
endif()

# nicer warnings
if (MSVC)
  target_compile_options(async_chat_server PRIVATE /W4 /permissive-)
  target_compile_options(chat_cli PRIVATE /W4 /permissive-)
else()
  target_compile_options(async_chat_server PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(chat_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
