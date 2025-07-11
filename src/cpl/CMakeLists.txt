# CPL (Command Processing Language) Module

# Set sources for the CPL module
set(CPL_SOURCES
    cpl_config_loader.cpp
)

set(CPL_HEADERS
    cpl_parser.h
    cpl_config_loader.h
    command_library.h
    cpl_executor.h
    llm_adapter.h
    external_config_loader.h
)

# Create CPL static library
add_library(burwell_cpl STATIC ${CPL_SOURCES} ${CPL_HEADERS})

# Set include directories
target_include_directories(burwell_cpl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# Link required libraries
target_link_libraries(burwell_cpl 
    burwell_common
)

# Set compiler features
target_compile_features(burwell_cpl PUBLIC cxx_std_17)

# Set compile definitions
target_compile_definitions(burwell_cpl PRIVATE
    BURWELL_CPL_MODULE
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(burwell_cpl PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# Install headers for development
install(FILES ${CPL_HEADERS}
    DESTINATION include/burwell/cpl
    COMPONENT Development
)

# Install library
install(TARGETS burwell_cpl
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)