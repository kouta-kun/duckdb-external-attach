cmake_minimum_required(VERSION 2.8.12)
project(attach_extension)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(IGNORE_WARNINGS OFF)

build_loadable_extension(
  attach_extension "-warnings" attach_duckdb.cpp)
