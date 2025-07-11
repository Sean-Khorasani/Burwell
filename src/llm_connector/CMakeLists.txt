add_library(burwell_llm_connector STATIC
    llm_connector.cpp
    http_client.cpp
)

target_include_directories(burwell_llm_connector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(burwell_llm_connector PUBLIC burwell_common)


