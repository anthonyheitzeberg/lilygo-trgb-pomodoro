cmake_minimum_required(VERSION 3.16)
 
# Pull in the ESP-IDF build system.
# IDF_PATH must be set in your environment (done automatically by idf.py).
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
 
project(pomodoro_timer)