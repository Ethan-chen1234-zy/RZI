# SPDX-FileCopyrightText: Copyright (c) 2026 RAKwireless
# SPDX-License-Identifier: Apache-2.0
#
# Standalone build of the uplink log library. Zephyr applications add the
# sources from src/ and the include/ directory to their own target instead.

cmake_minimum_required(VERSION 3.20.0)
project(uplink_log C)

set(UPLINK_LOG_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/src/uplink_log.c
  ${CMAKE_CURRENT_LIST_DIR}/src/uplink_log_frame.c
)

add_library(uplink_log STATIC ${UPLINK_LOG_SOURCES})
target_include_directories(uplink_log PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
set_target_properties(uplink_log PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_compile_options(uplink_log PRIVATE -Wall -Wextra -Wpedantic)
