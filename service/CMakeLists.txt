# Burwell Service Manager
add_executable(burwell-service
    burwell_service_cli.cpp
    service_manager.cpp
)

target_include_directories(burwell-service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Windows-specific libraries for service management
if(WIN32)
    target_link_libraries(burwell-service PRIVATE
        advapi32  # For service management APIs
    )
endif()

# Set output directory to build/bin
set_target_properties(burwell-service PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)