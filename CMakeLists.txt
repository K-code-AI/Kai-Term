# Kai-Term build scaffold (M0)
cmake_minimum_required(VERSION 3.5)
project(Kai-Term VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core)

add_executable(kai-term
    src/app/main.cpp
    src/terminal/vtparser.cpp
    src/platform/platform.cpp
)

target_link_libraries(kai-term PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)

add_executable(kai-term-parser
    tests/parser.cpp
    src/terminal/vtparser.cpp
)

add_executable(kai-term-pty
    tests/pty.cpp
    src/terminal/vtparser.cpp
)

add_executable(kai-term-tmux-e2e
    tests/tmux_e2e.cpp
    src/terminal/vtparser.cpp
)
if(EXISTS "/usr/lib64/libutil.a" OR EXISTS "/usr/lib64/libutil.so" OR EXISTS "/usr/lib/libutil.a")
    target_link_libraries(kai-term-tmux-e2e PRIVATE util)
endif()

target_compile_definitions(kai-term PRIVATE KAI_TERM_VERSION="0.0.0")
