add_library(burwell_orchestrator STATIC
    orchestrator.cpp
    execution_engine.cpp
    state_manager.cpp
    event_manager.cpp
    script_manager.cpp
    feedback_controller.cpp
    conversation_manager.cpp
    orchestrator_facade.cpp
)

target_include_directories(burwell_orchestrator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(burwell_orchestrator PUBLIC burwell_common)


