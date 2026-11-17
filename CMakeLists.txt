# Kai-Term build scaffold (M0)
# Qt 6's own CMake package needs 3.16, so that is the honest floor.
cmake_minimum_required(VERSION 3.16)
project(Kai-Term VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The class of bug this code keeps running into — a view left pointing at a
# temporary, a member initialised out of order — is the class the compiler can
# see. Qt's own headers trip one diagnostic that has nothing to do with us.
add_compile_options(-Wall -Wextra -Wno-sfinae-incomplete)

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

# forkpty() lives in libutil on glibc systems.
find_library(UTIL_LIBRARY util)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-tmux-e2e PRIVATE ${UTIL_LIBRARY})
endif()

# Keyboard encoding, asserted on the exact bytes sent to the PTY.
add_executable(kai-term-input
    tests/input.cpp
)
target_link_libraries(kai-term-input PRIVATE Qt6::Core)

# The same sequences replayed through a real PTY, read back from the other end.
add_executable(kai-term-input-pty
    tests/input_pty.cpp
)
target_link_libraries(kai-term-input-pty PRIVATE Qt6::Core)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-input-pty PRIVATE ${UTIL_LIBRARY})
endif()

# Scrollback model behind the scrollback UI.
add_executable(kai-term-scrollback
    tests/scrollback.cpp
    src/terminal/vtparser.cpp
)

# Mouse encoding, asserted on the exact bytes sent to the PTY.
add_executable(kai-term-mouse
    tests/mouse.cpp
)

# Selection geometry and the text a selection turns into.
add_executable(kai-term-selection
    tests/selection.cpp
    src/terminal/vtparser.cpp
)

# Offscreen widget exercise: real widget, real shell, no display server.
add_executable(kai-term-widget-smoke
    tests/widget_smoke.cpp
    src/terminal/vtparser.cpp
)
target_link_libraries(kai-term-widget-smoke PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-widget-smoke PRIVATE ${UTIL_LIBRARY})
endif()

enable_testing()
add_test(NAME parser COMMAND kai-term-parser)
add_test(NAME pty COMMAND kai-term-pty)
add_test(NAME tmux-e2e COMMAND kai-term-tmux-e2e)
add_test(NAME input COMMAND kai-term-input)
add_test(NAME input-pty COMMAND kai-term-input-pty)
add_test(NAME scrollback COMMAND kai-term-scrollback)
add_test(NAME mouse COMMAND kai-term-mouse)
add_test(NAME selection COMMAND kai-term-selection)

add_executable(kai-term-scrollbar tests/scrollbar.cpp)
target_link_libraries(kai-term-scrollbar PRIVATE Qt6::Gui Qt6::Core)
add_test(NAME scrollbar COMMAND kai-term-scrollbar)

add_executable(kai-term-process tests/process.cpp)
add_test(NAME process COMMAND kai-term-process)

# Daemon protocol: framing, field escaping, record lists. Pure bytes, no sockets.
add_executable(kai-term-ipc tests/ipc.cpp)
add_test(NAME ipc COMMAND kai-term-ipc)

# The Unix-socket layer under the daemon: real sockets, real filesystem, no display.
add_executable(kai-term-sockets tests/sockets.cpp)
add_test(NAME sockets COMMAND kai-term-sockets)

# Screen snapshots: what a window attaching to a running session is shown.
add_executable(kai-term-snapshot tests/snapshot.cpp src/terminal/vtparser.cpp)
add_test(NAME snapshot COMMAND kai-term-snapshot)

add_executable(kai-term-mouse-app tests/mouse_app.cpp src/terminal/vtparser.cpp)
target_link_libraries(kai-term-mouse-app PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)

add_test(NAME mouse-app COMMAND kai-term-mouse-app)
add_test(NAME widget-smoke COMMAND kai-term-widget-smoke)

target_compile_definitions(kai-term PRIVATE KAI_TERM_VERSION="0.0.0")
