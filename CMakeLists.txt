cmake_minimum_required(VERSION 3.11)
project(c_test C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)

# Options
option(USE_RAYLIB "Use raylib" ON)
option(USE_MINIMAL_RAYLIB_MODULES "Use minimal raylib modules" ON)
option(USE_IMGUI "Use ImGui" OFF)
option(USE_NO_ASSETS "Use no assets" OFF)
option(USE_LDTKLOADER "Use LDtk Loader" OFF)
option(USE_FMT "Use fmt" OFF)
option(USE_BOX2D "Use Box2D" OFF)
option(USE_BULLET3 "Use Bullet Physics 3D" OFF)
option(USE_REACTPHYSICS3D "Use React Physics 3D" OFF)
option(USE_JOLTPHYSICS "Use Jolt Physics 3D" OFF)

include(FetchContent)

# Set FetchContent to use a persistent directory for downloads
set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps CACHE PATH "FetchContent base directory")
set(FETCHCONTENT_QUIET OFF)  # Show download progress
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)  # Prevent re-downloading if already present

# Flecs
FetchContent_Declare(
        flecs
        GIT_REPOSITORY https://github.com/SanderMertens/flecs
        GIT_TAG v4.1.1
        GIT_SHALLOW TRUE  # Only clone the specific tag, not full history
        GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(flecs)

# Raylib
if(USE_RAYLIB)
    # Configure raylib build options based on your preferences
    if(USE_MINIMAL_RAYLIB_MODULES)
        set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
        set(SUPPORT_MODULE_RSHAPES ON CACHE BOOL "" FORCE)
        set(SUPPORT_MODULE_RTEXTURES ON CACHE BOOL "" FORCE)
        set(SUPPORT_MODULE_RTEXT ON CACHE BOOL "" FORCE)
        set(SUPPORT_MODULE_RMODELS OFF CACHE BOOL "" FORCE)
        set(SUPPORT_MODULE_RAUDIO ON CACHE BOOL "" FORCE)
    endif()

    # Common raylib options
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(OPENGL_VERSION "3.3" CACHE STRING "" FORCE)

    # Platform-specific settings
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        # macOS specific raylib settings
        set(SUPPORT_HIGHDPI ON CACHE BOOL "" FORCE)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        set(USE_WAYLAND OFF CACHE BOOL "" FORCE) # Set to ON if you want Wayland support
    endif()

    FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE  # Shows git clone progress
    )
    FetchContent_MakeAvailable(raylib)
endif()

# Main application executable
add_executable(c_test src/main.c
        src/audio.c
        src/spatial.c)

# Link libraries
target_link_libraries(c_test PRIVATE flecs::flecs_static)

if(USE_RAYLIB)
    target_link_libraries(c_test PRIVATE raylib)

    # Optional: Add raylib include directories explicitly (usually not needed)
    # target_include_directories(c_test PRIVATE ${raylib_SOURCE_DIR}/src)

    # Platform-specific dependencies
    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        find_package(Threads REQUIRED)
        target_link_libraries(c_test PRIVATE ${CMAKE_THREAD_LIBS_INIT})
        target_link_libraries(c_test PRIVATE m)  # Math library
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_link_libraries(c_test PRIVATE "-framework CoreVideo" "-framework IOKit" "-framework Cocoa" "-framework GLUT" "-framework OpenGL")
    elseif(WIN32)
        target_link_libraries(c_test PRIVATE winmm)
    endif()
endif()

# Test executable (separate from main app)
add_executable(test_runner
        tests/test_framework.h
        tests/test_main.c
        tests/test_module1.c)

# Enable CTest support
enable_testing()
add_test(NAME AllTests COMMAND test_runner)