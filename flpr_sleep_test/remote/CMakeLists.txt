cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote)

message(DEBUG "Power mode handler for RISC-V is included.")
target_sources(app PRIVATE src/power_off.c)
target_sources(app PRIVATE src/main_flpr.c)