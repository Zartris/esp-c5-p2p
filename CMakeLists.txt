# ESP-IDF CMake project for ESP32-C5 P2P Application
cmake_minimum_required(VERSION 3.16)

# Set the target chip for ESP32-C5
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-c5-p2p)