cmake_minimum_required(VERSION 3.5)
project(neoaconnect)
add_definitions("-std=gnu++20 -Wall")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)

include(FindPkgConfig)
pkg_check_modules(ALSA REQUIRED alsa)

# include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(
    neoaconnect
    neoaconnect.cpp
)

target_link_libraries(neoaconnect ${ALSA_LIBRARIES})
target_include_directories(neoaconnect PUBLIC ${ALSA_INCLUDE_DIRS})
target_compile_options(neoaconnect PUBLIC ${ALSA_CFLAGS_OTHER})
