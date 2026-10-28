cmake_minimum_required(VERSION 3.25)
project(wx_GO_MIDI_CPP)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O1 /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF /MANIFEST:NO")
    # 静态链接 VC++ 运行时，避免依赖 vcruntime140.dll / msvcp140.dll
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "MSVC runtime" FORCE)
# MinGW/GCC optimization flags
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os -DNDEBUG -ffunction-sections -fdata-sections")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s -Wl,--gc-sections")
endif()

if(MINGW)
    # Statically link libgcc and libstdc++ to avoid dependencies
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

# ============================================================================
# wxWidgets Source Build Configuration
# ============================================================================
set(wxWidgets_ROOT_DIR "${CMAKE_SOURCE_DIR}/wxWidgets-3.3.1")

# Build wxWidgets as static library
set(wxBUILD_SHARED OFF CACHE BOOL "Build wxWidgets as static library" FORCE)

# Disable unnecessary wxWidgets features to reduce binary size
set(wxUSE_EXCEPTIONS OFF CACHE BOOL "Disable exceptions" FORCE)
set(wxUSE_RTTI OFF CACHE BOOL "Disable RTTI" FORCE)
set(wxUSE_STL ON CACHE BOOL "Use STL" FORCE)
set(wxUSE_STD_CONTAINERS ON CACHE BOOL "Use standard containers" FORCE)

# Disable unneeded features
set(wxUSE_AUI OFF CACHE BOOL "Disable AUI" FORCE)
set(wxUSE_RIBBON OFF CACHE BOOL "Disable Ribbon" FORCE)
set(wxUSE_PROPGRID OFF CACHE BOOL "Disable PropertyGrid" FORCE)
set(wxUSE_GRID OFF CACHE BOOL "Disable Grid" FORCE)
set(wxUSE_HTML OFF CACHE BOOL "Disable HTML" FORCE)
set(wxUSE_WEBVIEW OFF CACHE BOOL "Disable WebView" FORCE)
set(wxUSE_RICHTEXT OFF CACHE BOOL "Disable RichText" FORCE)
set(wxUSE_MEDIACTRL OFF CACHE BOOL "Disable MediaCtrl" FORCE)
set(wxUSE_STC OFF CACHE BOOL "Disable STC" FORCE)
set(wxUSE_XRC OFF CACHE BOOL "Disable XRC" FORCE)
set(wxUSE_XML OFF CACHE BOOL "Disable XML" FORCE)
set(wxUSE_FSVOLUME OFF CACHE BOOL "Disable FS Volume" FORCE)
set(wxUSE_FSWATCHER OFF CACHE BOOL "Disable FS Watcher" FORCE)
set(wxUSE_DEBUGREPORT OFF CACHE BOOL "Disable Debug Report" FORCE)
set(wxUSE_SNGLINST_CHECKER OFF CACHE BOOL "Disable Single Instance Checker" FORCE)
set(wxUSE_ON_FATAL_EXCEPTION OFF CACHE BOOL "Disable Fatal Exception" FORCE)
set(wxUSE_STACKWALKER OFF CACHE BOOL "Disable Stack Walker" FORCE)
set(wxUSE_CRASHREPORT OFF CACHE BOOL "Disable Crash Report" FORCE)
set(wxUSE_DRAGIMAGE OFF CACHE BOOL "Disable Drag Image" FORCE)
set(wxUSE_DRAG_AND_DROP ON CACHE BOOL "Enable Drag and Drop" FORCE)
set(wxUSE_CMDLINE_PARSER OFF CACHE BOOL "Disable Command Line Parser" FORCE)
set(wxUSE_TEXTFILE ON CACHE BOOL "Enable TextFile (required by wxConfig)" FORCE)
set(wxUSE_FILESYSTEM OFF CACHE BOOL "Disable Filesystem" FORCE)
set(wxUSE_FS_INET OFF CACHE BOOL "Disable Inet FS" FORCE)
set(wxUSE_FS_ZIP OFF CACHE BOOL "Disable Zip FS" FORCE)
set(wxUSE_ZIPSTREAM OFF CACHE BOOL "Disable Zip Stream" FORCE)
set(wxUSE_TARSTREAM OFF CACHE BOOL "Disable Tar Stream" FORCE)
set(wxUSE_UNICODE ON CACHE BOOL "Enable Unicode" FORCE)
set(wxUSE_UTF8_LOCALE ON CACHE BOOL "Use UTF8 locale" FORCE)
set(wxUSE_PRINTF_POS_PARAMS ON CACHE BOOL "Use printf pos params" FORCE)
set(wxUSE_ZLIB OFF CACHE BOOL "Disable zlib" FORCE)
set(wxUSE_LIBPNG OFF CACHE BOOL "Disable libpng" FORCE)
set(wxUSE_LIBJPEG OFF CACHE BOOL "Disable libjpeg" FORCE)
set(wxUSE_LIBTIFF OFF CACHE BOOL "Disable libtiff" FORCE)
set(wxUSE_EXPAT OFF CACHE BOOL "Disable expat" FORCE)
set(wxUSE_REGEX OFF CACHE BOOL "Disable regex" FORCE)
set(wxUSE_LIBWEBP OFF CACHE BOOL "Disable libwebp" FORCE)
set(wxUSE_NANOSVG OFF CACHE BOOL "Disable NanoSVG" FORCE)
set(wxUSE_LIBLZMA OFF CACHE BOOL "Disable liblzma" FORCE)
set(wxUSE_SVG OFF CACHE BOOL "Disable SVG" FORCE)

# Disable GUI extras
set(wxUSE_ARTPROVIDER_STD OFF CACHE BOOL "Disable standard art provider" FORCE)
set(wxUSE_ARTPROVIDER_TANGO OFF CACHE BOOL "Disable Tango art provider" FORCE)
set(wxUSE_HELP OFF CACHE BOOL "Disable Help" FORCE)
set(wxUSE_MS_HTML_HELP OFF CACHE BOOL "Disable MS HTML Help" FORCE)
set(wxUSE_WXHTML_HELP OFF CACHE BOOL "Disable wxHTML Help" FORCE)

# Enable required features (needed by other disabled features)
set(wxUSE_CONFIG ON CACHE BOOL "Enable Config" FORCE)
set(wxUSE_DATAOBJ ON CACHE BOOL "Enable DataObject (required by OLE)" FORCE)
set(wxUSE_DATETIME ON CACHE BOOL "Enable DateTime (required by Archive)" FORCE)
set(wxUSE_FILE ON CACHE BOOL "Enable File (required)" FORCE)
set(wxUSE_SOCKETS ON CACHE BOOL "Enable Sockets (required by protocols)" FORCE)
set(wxUSE_IPV6 OFF CACHE BOOL "Disable IPv6" FORCE)
set(wxUSE_PROTOCOL OFF CACHE BOOL "Disable Protocol" FORCE)
set(wxUSE_PROTOCOL_HTTP OFF CACHE BOOL "Disable HTTP Protocol" FORCE)
set(wxUSE_PROTOCOL_FTP OFF CACHE BOOL "Disable FTP Protocol" FORCE)
set(wxUSE_PROTOCOL_FILE OFF CACHE BOOL "Disable File Protocol" FORCE)
set(wxUSE_URL OFF CACHE BOOL "Disable URL" FORCE)
set(wxUSE_ARCHIVE OFF CACHE BOOL "Disable Archive" FORCE)

# Build only core and base libraries
set(wxBUILD_MONOLITHIC OFF CACHE BOOL "Build monolithic library" FORCE)

# Add wxWidgets source directory
add_subdirectory(${wxWidgets_ROOT_DIR} wxWidgets)

# Include directories
include_directories(src)

# Explicit source list: keeps incremental builds correct when files are added,
# removed, or renamed. Glob-based source discovery requires a manual reconfigure.
set(SOURCES
    src/App.cpp
    src/App.h
    src/core/KeyboardSimulator.cpp
    src/core/KeyboardSimulator.h
    src/core/PlaybackEngine.cpp
    src/core/PlaybackEngine.h
    src/core/TransposePlanner.cpp
    src/core/TransposePlanner.h
    src/midi/MidiParser.cpp
    src/midi/MidiParser.h
    src/ui/KeymapEditorDialog.cpp
    src/ui/KeymapEditorDialog.h
    src/ui/KeymapSchemeConfig.cpp
    src/ui/KeymapSchemeConfig.h
    src/ui/MainFrame.cpp
    src/ui/MainFrame.h
    src/ui/PianoRollCtrl.cpp
    src/ui/PianoRollCtrl.h
    src/ui/PianoRollModel.cpp
    src/ui/PianoRollModel.h
    src/ui/PlaybackState.cpp
    src/ui/PlaybackState.h
    src/ui/UIHelpers.cpp
    src/ui/UIHelpers.h
    src/ui/Widgets.cpp
    src/ui/Widgets.h
    src/util/KeyManager.cpp
    src/util/KeyManager.h
    src/util/Logger.cpp
    src/util/Logger.h
    src/util/NtpClient.cpp
    src/util/NtpClient.h
    src/util/PlaylistManager.cpp
    src/util/PlaylistManager.h
)

# Add resource file for Windows Visual Styles (manifest)
if(WIN32)
    list(APPEND SOURCES "src/resource.rc")
endif()

# Executable
add_executable(wx_GO_MIDI_CPP WIN32 ${SOURCES})
set_target_properties(wx_GO_MIDI_CPP PROPERTIES OUTPUT_NAME "GO_MIDI!")

function(go_midi_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
    endif()
endfunction()

go_midi_enable_warnings(wx_GO_MIDI_CPP)

# Link with wxWidgets (built from source)
target_link_libraries(wx_GO_MIDI_CPP wx::core wx::base)

# Link Windows system libraries
target_link_libraries(wx_GO_MIDI_CPP
    gdiplus
    msimg32
    oleacc
    uxtheme
    gdi32
    comctl32
    rpcrt4
    winmm
    ws2_32
    wininet
    winhttp
    uuid
    ole32
    oleaut32
    comdlg32
    user32
    kernel32
    advapi32
    shell32
    imm32
    version
    shlwapi
    psapi
)

option(GO_MIDI_BUILD_TESTS "Build pure core tests" ON)
if(GO_MIDI_BUILD_TESTS)
    enable_testing()

    if(CMAKE_CROSSCOMPILING AND CMAKE_HOST_UNIX)
        find_program(GO_MIDI_WINE_EXECUTABLE wine)
        if(GO_MIDI_WINE_EXECUTABLE)
            set(CMAKE_CROSSCOMPILING_EMULATOR "${GO_MIDI_WINE_EXECUTABLE}")
        endif()
    endif()

    add_executable(transpose_planner_tests
        tests/TransposePlannerTests.cpp
        src/core/TransposePlanner.cpp
    )
    target_include_directories(transpose_planner_tests PRIVATE src)
    go_midi_enable_warnings(transpose_planner_tests)
    add_test(NAME transpose_planner_tests COMMAND transpose_planner_tests)

    add_executable(key_manager_tests
        tests/KeyManagerTests.cpp
        src/util/KeyManager.cpp
        src/util/Logger.cpp
    )
    target_include_directories(key_manager_tests PRIVATE src)
    target_link_libraries(key_manager_tests user32 kernel32)
    go_midi_enable_warnings(key_manager_tests)
    add_test(NAME key_manager_tests COMMAND key_manager_tests)

    add_executable(keymap_scheme_config_tests
        tests/KeymapSchemeConfigTests.cpp
        src/ui/KeymapSchemeConfig.cpp
    )
    target_include_directories(keymap_scheme_config_tests PRIVATE src)
    target_link_libraries(keymap_scheme_config_tests wx::core wx::base)
    go_midi_enable_warnings(keymap_scheme_config_tests)
    add_test(NAME keymap_scheme_config_tests COMMAND keymap_scheme_config_tests)

    add_executable(piano_roll_model_tests
        tests/PianoRollModelTests.cpp
        src/ui/PianoRollModel.cpp
    )
    target_include_directories(piano_roll_model_tests PRIVATE src)
    go_midi_enable_warnings(piano_roll_model_tests)
    add_test(NAME piano_roll_model_tests COMMAND piano_roll_model_tests)

    add_executable(piano_roll_ctrl_tests
        tests/PianoRollCtrlTests.cpp
        src/ui/PianoRollCtrl.cpp
        src/ui/PianoRollModel.cpp
    )
    target_include_directories(piano_roll_ctrl_tests PRIVATE src)
    target_link_libraries(piano_roll_ctrl_tests wx::core wx::base imm32 user32 kernel32)
    go_midi_enable_warnings(piano_roll_ctrl_tests)
    if(CMAKE_CROSSCOMPILING AND CMAKE_HOST_UNIX)
        find_program(GO_MIDI_XVFB_RUN xvfb-run)
        if(GO_MIDI_XVFB_RUN AND GO_MIDI_WINE_EXECUTABLE)
            add_test(NAME piano_roll_ctrl_tests
                     COMMAND ${GO_MIDI_XVFB_RUN} -a ${GO_MIDI_WINE_EXECUTABLE}
                             $<TARGET_FILE:piano_roll_ctrl_tests>)
        else()
            add_test(NAME piano_roll_ctrl_tests COMMAND piano_roll_ctrl_tests)
        endif()
    else()
        add_test(NAME piano_roll_ctrl_tests COMMAND piano_roll_ctrl_tests)
    endif()

    add_executable(midi_parser_tests
        tests/MidiParserTests.cpp
        src/midi/MidiParser.cpp
        src/util/Logger.cpp
    )
    target_include_directories(midi_parser_tests PRIVATE src)
    target_link_libraries(midi_parser_tests user32 kernel32)
    go_midi_enable_warnings(midi_parser_tests)
    add_test(NAME midi_parser_tests
             COMMAND midi_parser_tests "${CMAKE_SOURCE_DIR}/tests/data/public_domain_midi")

    add_executable(transpose_regression_tests
        tests/TransposeRegressionTests.cpp
        src/core/TransposePlanner.cpp
        src/midi/MidiParser.cpp
        src/util/KeyManager.cpp
        src/util/Logger.cpp
    )
    target_include_directories(transpose_regression_tests PRIVATE src)
    target_link_libraries(transpose_regression_tests user32 kernel32)
    go_midi_enable_warnings(transpose_regression_tests)
    add_test(NAME transpose_regression_tests
             COMMAND transpose_regression_tests "${CMAKE_SOURCE_DIR}/tests/data/public_domain_midi")

    set(GO_MIDI_TESTS
        transpose_planner_tests
        key_manager_tests
        keymap_scheme_config_tests
        piano_roll_model_tests
        midi_parser_tests
        transpose_regression_tests
    )
    # GitHub's non-interactive Windows service session cannot host a wx GUI
    # message loop reliably. Keep the event test available for local/Wine runs.
    if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
        set_tests_properties(piano_roll_ctrl_tests PROPERTIES DISABLED TRUE)
    else()
        list(APPEND GO_MIDI_TESTS piano_roll_ctrl_tests)
    endif()
    set_tests_properties(${GO_MIDI_TESTS} PROPERTIES TIMEOUT 60)
    set_tests_properties(piano_roll_ctrl_tests PROPERTIES TIMEOUT 120)

    add_executable(transpose_compare
        tools/transpose_compare.cpp
        src/core/TransposePlanner.cpp
        src/midi/MidiParser.cpp
        src/util/KeyManager.cpp
        src/util/Logger.cpp
    )
    target_include_directories(transpose_compare PRIVATE src)
    target_link_libraries(transpose_compare user32 kernel32)
    go_midi_enable_warnings(transpose_compare)

    add_executable(planner_memory_benchmark
        tools/planner_memory_benchmark.cpp
        src/core/TransposePlanner.cpp
        src/midi/MidiParser.cpp
        src/util/KeyManager.cpp
        src/util/Logger.cpp
    )
    target_include_directories(planner_memory_benchmark PRIVATE src)
    target_link_libraries(planner_memory_benchmark user32 kernel32 psapi)
    go_midi_enable_warnings(planner_memory_benchmark)

    add_custom_target(validate
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${GO_MIDI_TESTS}
        USES_TERMINAL
    )
endif()

# Link stdc++fs for std::filesystem support (GCC 8 and below need this)
if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(wx_GO_MIDI_CPP stdc++fs)
endif()
