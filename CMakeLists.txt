cmake_minimum_required(VERSION 3.10)
project(wx_GO_MIDI_CPP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O1 /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
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

# Source files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp" 
    "src/*.h"
)

# Add resource file for Windows Visual Styles (manifest)
if(WIN32)
    list(APPEND SOURCES "src/resource.rc")
endif()

# Executable
add_executable(wx_GO_MIDI_CPP WIN32 ${SOURCES})
set_target_properties(wx_GO_MIDI_CPP PROPERTIES OUTPUT_NAME "GO_MIDI!")

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
