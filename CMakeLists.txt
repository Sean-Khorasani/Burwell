cmake_minimum_required(VERSION 3.15)

project(Burwell VERSION 0.1.0 LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BURWELL_NO_GDIPLUS "Build without GDI+ support (for compatibility)" OFF)

# Compiler warning flags for better code quality
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
    # Use static runtime to avoid MSVC redistributables
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    # For MinGW, minimize runtime dependencies
    add_compile_options(-Wall -Wextra -Werror)
    add_compile_options(-Wno-unknown-pragmas)
    
    # Link only to Windows system libraries, avoid MinGW runtime
    if(MINGW)
        # Use Windows native libraries directly
        add_compile_options(-D_WIN32_WINNT=0x0601)  # Windows 7 minimum
        # Static linking to avoid runtime dependencies
        add_link_options(-static-libgcc -static-libstdc++ -static)
    endif()
endif()

# Security-related compiler flags
if(NOT MSVC)
    add_compile_options(-D_FORTIFY_SOURCE=2 -fstack-protector-strong)
    # Position Independent Executable flags will be set per-target
endif()

# Find required packages
# For cross-compilation, we'll use the header-only version
if(CMAKE_CROSSCOMPILING)
    # Use the nlohmann/json header directly
    include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/include)
else()
    find_package(nlohmann_json 3.11.0 REQUIRED)
endif()

# For now, we'll implement a simple HTTP client instead of cpr
# to avoid complex dependency issues during development

# Configure build types
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel.")
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

# Set output directories - keep build artifacts flat, not mirroring source structure
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Prevent CMake from creating source directory structure in build
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

# Add subdirectories for each module 
# Use standard CMake behavior - intermediate files go to build/src/ (this is normal)
# Final artifacts go to build/bin/ and build/lib/ (controlled by output directories above)
add_subdirectory(src/common)
add_subdirectory(src/cpl)
add_subdirectory(src/ocal)
add_subdirectory(src/orchestrator)
add_subdirectory(src/llm_connector)
add_subdirectory(src/command_parser)
add_subdirectory(src/task_engine)
add_subdirectory(src/ui_module)
add_subdirectory(src/environmental_perception)

# Main executable
add_executable(burwell src/main.cpp)

# Link the main executable with all the module libraries and dependencies
target_link_libraries(burwell
    burwell_common
    burwell_cpl
    burwell_orchestrator
    burwell_llm_connector
    burwell_command_parser
    burwell_ocal
    burwell_task_engine
    burwell_ui_module
    burwell_environmental_perception
)

# Windows-specific libraries for OS control
if(WIN32)
    target_link_libraries(burwell
        user32
        kernel32
        gdi32
        ole32
        shell32
        wininet  # For HTTP requests on Windows
        gdiplus  # For GDI+ functions in environmental perception
    )
endif()

# Copy configuration files and test scripts to build directory
# This ensures the burwell.exe can find its config files and test scripts
add_custom_target(copy_resources ALL
    COMMENT "Copying configuration files and test scripts to build directory"
)

# Copy config directory
add_custom_command(TARGET copy_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/config
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config
    COMMENT "Copying config directory"
)

# Copy test_scripts directory
add_custom_command(TARGET copy_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/test_scripts
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/test_scripts
    COMMENT "Copying test_scripts directory"
)

# Make burwell depend on copy_resources
add_dependencies(burwell copy_resources)



