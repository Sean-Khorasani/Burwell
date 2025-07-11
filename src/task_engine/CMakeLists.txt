add_library(burwell_task_engine STATIC
    task_engine.cpp
)

target_include_directories(burwell_task_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(burwell_task_engine PUBLIC burwell_common)


