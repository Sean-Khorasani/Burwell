add_library(burwell_command_parser STATIC
    command_parser.cpp
)

target_include_directories(burwell_command_parser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../task_engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../llm_connector
)

target_link_libraries(burwell_command_parser PUBLIC burwell_common)


