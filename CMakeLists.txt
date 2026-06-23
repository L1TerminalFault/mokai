
cmake_minimum_required(VERSION 3.20)
project(MOKAI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Gather all core sources
file(GLOB_RECURSE SOURCES "src/**/*.cpp")
file(GLOB_RECURSE HEADERS "src/**/*.hpp")

list(FILTER SOURCES EXCLUDE REGEX "src/templates/.*")
list(FILTER HEADERS EXCLUDE REGEX "src/templates/.*")

# 3. Define the static engine library
add_library(MOKAI STATIC ${SOURCES} ${HEADERS})

target_include_directories(MOKAI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(MOKAI PRIVATE
    MOKAI_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/templates"
)

# 5. Define the runner binary executable
add_executable(mokai src/entery.cpp)
target_link_libraries(mokai PRIVATE MOKAI)
