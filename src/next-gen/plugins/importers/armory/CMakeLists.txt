project(armory)
cmake_minimum_required(VERSION 2.6)

message(STATUS "Using armory importer")

file(GLOB src *.cpp )

add_library(armory STATIC ${src})

        
