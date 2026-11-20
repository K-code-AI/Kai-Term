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

# The daemon that keeps sessions alive. No Qt: it is a socket, a pty and a screen
# model, and it has no business needing a display server to start.

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

find_package(Threads REQUIRED)

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

# The session daemon, with real pty and real sockets and no display. std::thread is
# what carries a session's output, so the test needs the thread library.
add_executable(kai-term-session tests/session.cpp src/terminal/vtparser.cpp)
target_link_libraries(kai-term-session PRIVATE Threads::Threads)
add_test(NAME session COMMAND kai-term-session)

# The words a person types, run through the real binary in a private runtime
# directory, so the command surface is checked against the program rather than
# against a re-reading of it.
add_executable(kai-term-cli-surface tests/cli_surface.cpp)
target_compile_definitions(kai-term-cli-surface PRIVATE KAI_TERM_BIN="$<TARGET_FILE:kai-term>")
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-cli-surface PRIVATE ${UTIL_LIBRARY})
endif()
add_test(NAME cli-surface COMMAND kai-term-cli-surface)

# A window rendering a session owned by the daemon: the backend seam, exercised with a
# real pty, a real socket and the offscreen platform.
add_executable(kai-term-widget-session
    tests/widget_session.cpp
    src/terminal/vtparser.cpp
)
target_link_libraries(kai-term-widget-session PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Threads::Threads)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-widget-session PRIVATE ${UTIL_LIBRARY})
endif()
add_test(NAME widget-session COMMAND kai-term-widget-session)


# What a second view of a session arrives knowing — the modes the program asked for, not
# only the glyphs on the grid. Each of these was found by Kaida from btop in a
# re-attached session: a snapshot that replays only text is a picture of a terminal
# rather than one.
add_executable(kai-term-attach-state
    tests/attach_state.cpp
    src/terminal/vtparser.cpp
)
target_link_libraries(kai-term-attach-state PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Threads::Threads)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-attach-state PRIVATE ${UTIL_LIBRARY})
endif()
add_test(NAME attach-state COMMAND kai-term-attach-state)

add_executable(kai-term-app-window
    tests/app_window.cpp
    src/terminal/vtparser.cpp
)
target_link_libraries(kai-term-app-window PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Threads::Threads)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-app-window PRIVATE ${UTIL_LIBRARY})
endif()
add_test(NAME app-window COMMAND kai-term-app-window)

add_executable(kai-term-close-prompt tests/close_prompt.cpp)
add_test(NAME close-prompt COMMAND kai-term-close-prompt)
add_executable(kai-term-config tests/config.cpp)
add_test(NAME config COMMAND kai-term-config)
add_executable(kai-term-theme-picker tests/theme_picker.cpp)
add_test(NAME theme-picker COMMAND kai-term-theme-picker)

# Closing the real binary under a real compositor. The close prompt was once "verified" by
# calling the window's close from a test, while the terminal sat inside a QMainWindow and
# never received a close from anything. This asks sway, with no display, to close the
# window the way a desktop does, and only runs where sway exists.
find_program(SWAY_EXECUTABLE sway)
if(SWAY_EXECUTABLE)
    add_test(NAME wayland-close
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/wayland_close.sh)
    set_tests_properties(wayland-close PROPERTIES
        ENVIRONMENT "KAI_TERM=$<TARGET_FILE:kai-term>"
        TIMEOUT 180)
endif()

# Pane-scoped selection: real mouse drags across a boundary the widget has been told
# about, with the offscreen platform.
add_executable(kai-term-widget-panes
    tests/widget_panes.cpp
    src/terminal/vtparser.cpp
)
target_link_libraries(kai-term-widget-panes PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-widget-panes PRIVATE ${UTIL_LIBRARY})
endif()
add_test(NAME widget-panes COMMAND kai-term-widget-panes)

add_executable(kai-term-mouse-app tests/mouse_app.cpp src/terminal/vtparser.cpp)
target_link_libraries(kai-term-mouse-app PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)

add_test(NAME mouse-app COMMAND kai-term-mouse-app)

# Only the application belongs in the build root. Everything else here is a test, and a
# person who has just built the project should find `kai-term` and no dozen siblings to
# wonder about. `add_test` finds these by target, so moving them changes nothing about
# how the suite runs.
set(KAI_TEST_TARGETS
    kai-term-parser
    kai-term-pty
    kai-term-tmux-e2e
    kai-term-input
    kai-term-input-pty
    kai-term-scrollback
    kai-term-mouse
    kai-term-selection
    kai-term-widget-smoke
    kai-term-scrollbar
    kai-term-process
    kai-term-ipc
    kai-term-sockets
    kai-term-snapshot
    kai-term-session
    kai-term-cli-surface
    kai-term-widget-session
    kai-term-attach-state
    kai-term-app-window
    kai-term-close-prompt
    kai-term-config
    kai-term-theme-picker
    kai-term-widget-panes
    kai-term-mouse-app)
foreach(kai_test ${KAI_TEST_TARGETS})
    set_target_properties(${kai_test} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                 ${CMAKE_BINARY_DIR}/tests)
endforeach()
add_test(NAME widget-smoke COMMAND kai-term-widget-smoke)

target_compile_definitions(kai-term PRIVATE KAI_TERM_VERSION="0.0.0")

# A window photographed instead of shown. Not a check: it writes pictures, for when the
# question is what a window would draw rather than what it knows.
add_executable(kai-term-grabview tests/tools/grabview.cpp src/terminal/vtparser.cpp)
target_link_libraries(kai-term-grabview PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Threads::Threads)
if(UTIL_LIBRARY)
    target_link_libraries(kai-term-grabview PRIVATE ${UTIL_LIBRARY})
endif()
