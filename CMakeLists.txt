cmake_minimum_required(VERSION 3.16)
project(pipelined_cpu_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pipeline
  src/main.cpp
  src/pipeline.cpp
)

target_include_directories(pipeline PRIVATE src)
