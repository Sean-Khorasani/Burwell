# Logging Migration TODO List

## Files Requiring Migration (31 files)

### Common Module (11 files)
- [ ] `src/common/config_manager.cpp`
- [ ] `src/common/credential_manager.cpp`
- [ ] `src/common/error_handler.cpp`
- [ ] `src/common/file_utils.cpp`
- [ ] `src/common/json_utils.cpp`
- [ ] `src/common/logger.cpp` (may need special handling)
- [ ] `src/common/logger_raii.cpp`
- [ ] `src/common/os_utils.cpp`
- [ ] `src/common/resource_monitor.cpp`
- [ ] `src/common/string_utils.cpp`
- [ ] `src/common/structured_logger.cpp` (verify if needed)

### OCAL Module (9 files)
- [ ] `src/ocal/filesystem_operations.cpp`
- [ ] `src/ocal/input_operations.cpp`
- [ ] `src/ocal/keyboard_control.cpp`
- [ ] `src/ocal/mouse_control.cpp`
- [ ] `src/ocal/ocal.cpp`
- [ ] `src/ocal/process_operations.cpp`
- [ ] `src/ocal/service_management.cpp`
- [ ] `src/ocal/window_management.cpp`
- [ ] `src/ocal/window_management_raii.cpp`

### Orchestrator Module (8 files)
- [ ] `src/orchestrator/conversation_manager.cpp`
- [ ] `src/orchestrator/event_manager.cpp`
- [ ] `src/orchestrator/execution_engine.cpp`
- [ ] `src/orchestrator/feedback_controller.cpp`
- [ ] `src/orchestrator/orchestrator_facade.cpp`
- [ ] `src/orchestrator/orchestrator_old.cpp` (consider removing if deprecated)
- [ ] `src/orchestrator/script_manager.cpp`
- [ ] `src/orchestrator/state_manager.cpp`

### LLM Connector Module (2 files)
- [ ] `src/llm_connector/http_client.cpp`
- [ ] `src/llm_connector/llm_connector.cpp` (partial - line 698)

### CPL Module (1 file)
- [ ] `src/cpl/cpl_config_loader.cpp`

## Migration Script Command

To migrate all files at once:
```bash
python convert_logging.py src/common/config_manager.cpp src/common/credential_manager.cpp src/common/error_handler.cpp src/common/file_utils.cpp src/common/json_utils.cpp src/common/logger_raii.cpp src/common/os_utils.cpp src/common/resource_monitor.cpp src/common/string_utils.cpp src/ocal/filesystem_operations.cpp src/ocal/input_operations.cpp src/ocal/keyboard_control.cpp src/ocal/mouse_control.cpp src/ocal/ocal.cpp src/ocal/process_operations.cpp src/ocal/service_management.cpp src/ocal/window_management.cpp src/ocal/window_management_raii.cpp src/orchestrator/conversation_manager.cpp src/orchestrator/event_manager.cpp src/orchestrator/execution_engine.cpp src/orchestrator/feedback_controller.cpp src/orchestrator/orchestrator_facade.cpp src/orchestrator/script_manager.cpp src/orchestrator/state_manager.cpp src/llm_connector/http_client.cpp src/llm_connector/llm_connector.cpp src/cpl/cpl_config_loader.cpp
```

## Special Cases

1. **src/common/logger.cpp** - Core logger implementation, may need manual review
2. **src/common/structured_logger.cpp** - Already structured logger, verify if migration needed
3. **src/orchestrator/orchestrator_old.cpp** - Consider removing if deprecated
4. **src/llm_connector/llm_connector.cpp** - Only line 698 needs migration

## Verification Steps

After migration:
1. Compile the project to catch any syntax errors
2. Search for any remaining Logger::log calls: `grep -r "Logger::log(" src/`
3. Search for any remaining LOG_* macros: `grep -r "LOG_DEBUG\|LOG_INFO\|LOG_WARNING\|LOG_ERROR" src/`
4. Test critical paths to ensure logging works correctly

## Success Criteria
- Zero instances of `Logger::log()` in active source files
- Zero instances of old LOG_* macros
- All logging uses SLOG_* structured logging
- Project compiles without errors
- Log output maintains same information but in structured format