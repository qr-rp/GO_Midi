cmake_minimum_required(VERSION 3.10)
project(wx_GO_MIDI_CPP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s -Wl,--gc-sections")

# Option for static linking (default ON for MinGW to avoid DLL hell)
option(USE_STATIC_WX "Use static wxWidgets libraries" ON)

if(MINGW)
    # Statically link libgcc and libstdc++ to avoid dependencies
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    
    if(USE_STATIC_WX)
        set(wxWidgets_USE_STATIC ON)
    endif()
endif()

# Set wxWidgets root path (REQUIRED for MinGW)
# 请修改为你实际的 wxWidgets 路径，或通过 -DwxWidgets_ROOT_DIR=... 传递
if(NOT DEFINED wxWidgets_ROOT_DIR)
    set(wxWidgets_ROOT_DIR "E:/workspace/GO_Midi/wxWidgets-3.3.1")
endif()

# MinGW specific: Directly configure wxWidgets paths
if(MINGW)
    set(wxWidgets_INCLUDE_DIRS "${wxWidgets_ROOT_DIR}/include;${wxWidgets_ROOT_DIR}/lib/gcc_lib/mswu")
    set(wxWidgets_LIB_DIR "${wxWidgets_ROOT_DIR}/lib/gcc_lib")
    
    # Manually set wxWidgets libraries for MinGW static linking
    # Order matters for static linking!
    set(wxWidgets_LIBRARIES
        ${wxWidgets_LIB_DIR}/libwxmsw33u_core.a
        ${wxWidgets_LIB_DIR}/libwxbase33u.a
        ${wxWidgets_LIB_DIR}/libwxpng.a
        ${wxWidgets_LIB_DIR}/libwxjpeg.a
        ${wxWidgets_LIB_DIR}/libwxtiff.a
        ${wxWidgets_LIB_DIR}/libwxzlib.a
        ${wxWidgets_LIB_DIR}/libwxregexu.a
        ${wxWidgets_LIB_DIR}/libwxexpat.a
        # Windows system libraries
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
    )
else()
    # Find wxWidgets for MSVC or other compilers
    find_package(wxWidgets REQUIRED COMPONENTS core base)
    include(${wxWidgets_USE_FILE})
endif()

# Include wxWidgets headers
include_directories(${wxWidgets_INCLUDE_DIRS})

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
# WIN32 option tells CMake to create a GUI application (WinMain entry point)
add_executable(wx_GO_MIDI_CPP WIN32 ${SOURCES})
set_target_properties(wx_GO_MIDI_CPP PROPERTIES OUTPUT_NAME "GO_MIDI!")

# Link libraries
if(MINGW)
    target_link_libraries(wx_GO_MIDI_CPP ${wxWidgets_LIBRARIES} psapi)
else()
    target_link_libraries(wx_GO_MIDI_CPP ${wxWidgets_LIBRARIES} winmm ws2_32 psapi winhttp)
endif()
