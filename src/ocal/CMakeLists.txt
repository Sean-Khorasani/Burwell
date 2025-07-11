add_library(burwell_ocal STATIC
    ocal.cpp
    mouse_control.cpp
    keyboard_control.cpp
    window_management.cpp
    application_automation.cpp
    service_management.cpp
    system_command.cpp
    filesystem_operations.cpp
    window_operations.cpp
    input_operations.cpp
    file_operations.cpp
    process_operations.cpp
)

target_include_directories(burwell_ocal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(burwell_ocal PUBLIC burwell_common)


