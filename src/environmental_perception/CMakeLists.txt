add_library(burwell_environmental_perception STATIC
    environmental_perception.cpp
)

target_include_directories(burwell_environmental_perception PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(burwell_environmental_perception PUBLIC burwell_common burwell_ocal)


