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

# Set wxWidgets root path hint (User should set this if not found)
# set(wxWidgets_ROOT_DIR "E:/path/to/wxWidgets-3.3.1")

# Find wxWidgets
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

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
target_link_libraries(wx_GO_MIDI_CPP ${wxWidgets_LIBRARIES} winmm ws2_32 psapi winhttp)
