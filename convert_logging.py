#!/usr/bin/env python3
"""
Script to convert old Logger::log calls to new structured logging calls.
"""

import re
import sys

def convert_log_level(old_level):
    """Convert old log level to new macro."""
    mapping = {
        'LogLevel::DEBUG': 'SLOG_DEBUG()',
        'LogLevel::INFO': 'SLOG_INFO()',
        'LogLevel::WARNING': 'SLOG_WARNING()', 
        'LogLevel::ERROR_LEVEL': 'SLOG_ERROR()',
        'LogLevel::CRITICAL': 'SLOG_CRITICAL()'
    }
    return mapping.get(old_level, 'SLOG_INFO()')

def convert_logger_log(line):
    """Convert Logger::log calls to structured logging."""
    # Match Logger::log(LogLevel::LEVEL, "message" [, file, line])
    pattern = r'Logger::log\s*\(\s*(LogLevel::\w+)\s*,\s*"([^"]*)"(?:\s*,\s*__FILE__\s*,\s*__LINE__)?\s*\);'
    
    def replacer(match):
        level = match.group(1)
        message = match.group(2)
        macro = convert_log_level(level)
        
        # Handle special cases
        if '[FAILED]' in message or '[ERROR]' in message:
            # Extract error context if present
            return f'{macro}.message("{message}");'
        elif '[OK]' in message or '[INFO]' in message:
            # Info messages with context
            return f'{macro}.message("{message}");'
        else:
            # Simple messages
            return f'{macro}.message("{message}");'
    
    # First try simple replacement
    new_line = re.sub(pattern, replacer, line)
    
    # Handle multi-line Logger::log calls
    if 'Logger::log' in line and new_line == line:
        # This might be a multi-line call or complex expression
        # For now, just flag it for manual review
        return line + " // TODO: Convert to structured logging"
    
    return new_line

def process_file(filepath):
    """Process a single file to convert logging."""
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    converted_lines = []
    for line in lines:
        converted_lines.append(convert_logger_log(line))
    
    with open(filepath, 'w') as f:
        f.writelines(converted_lines)
    
    print(f"Processed: {filepath}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python convert_logging.py <file1> [file2] ...")
        sys.exit(1)
    
    for filepath in sys.argv[1:]:
        try:
            process_file(filepath)
        except Exception as e:
            print(f"Error processing {filepath}: {e}")