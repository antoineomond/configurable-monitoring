cmake_minimum_required(VERSION 3.18)

project(lora_spi_receiver CXX)

# RadioLib requires C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../external/RadioLib" "${CMAKE_CURRENT_BINARY_DIR}/RadioLib")

add_executable(${PROJECT_NAME} lora_spi_receiver.cpp)

target_link_libraries(${PROJECT_NAME} RadioLib lgpio)

# RadioLib debug output, uncomment if needed
#target_compile_definitions(RadioLib PUBLIC RADIOLIB_DEBUG_BASIC RADIOLIB_DEBUG_SPI)
#target_compile_definitions(RadioLib PUBLIC RADIOLIB_DEBUG_PORT=stdout)
