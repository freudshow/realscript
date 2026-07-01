# ============================================================================
# CMakeLists.txt -- CMake build configuration for RealScript
#
# Build:
#   mkdir build && cd build && cmake .. && make
#
# Mirrors the existing Makefile: C99, -Wall -Wextra, debug symbols.
# ============================================================================

cmake_minimum_required(VERSION 3.10)

# Project name and language
project(realscript C)

# C99 standard (required by the codebase)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warning flags matching Makefile's -Wall -Wextra
target_compile_options(realscript PRIVATE -Wall -Wextra)

# Debug symbols matching Makefile's -g
set(CMAKE_BUILD_TYPE Debug)

# All source files in the project
set(SOURCES
    db.c
    value.c
    lexer.c
    ast.c
    parser.c
    compiler.c
    vm.c
    main.c
)

# Build the executable
add_executable(realscript ${SOURCES})
