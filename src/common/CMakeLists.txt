add_library(burwell_common STATIC
    config_manager.cpp
    credential_manager.cpp
    input_validator.cpp
    error_handler.cpp
    os_utils.cpp
    file_utils.cpp
    string_utils.cpp
    json_utils.cpp
    structured_logger.cpp
    service_factory.cpp
    resource_monitor.cpp
    thread_pool.cpp
)

target_include_directories(burwell_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link with Windows debug libraries for symbol functions
if(WIN32)
    target_link_libraries(burwell_common PUBLIC dbghelp)
endif()


