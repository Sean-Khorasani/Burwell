add_library(burwell_ui_module STATIC
    ui_module.cpp
)

target_include_directories(burwell_ui_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(burwell_ui_module PUBLIC burwell_common)


