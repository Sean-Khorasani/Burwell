# Burwell User Guide

## Table of Contents
1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Script System](#script-system)
4. [Command Reference](#command-reference)
5. [Conditional Commands](#conditional-commands)
6. [Control Flow Commands](#control-flow-commands)
7. [UIA Commands](#uia-commands)
8. [Examples](#examples)

## Introduction

Burwell is an AI-powered desktop automation agent that executes automation tasks through JSON scripts. This guide covers how to create and use automation scripts.

## Getting Started

### Running Burwell

```bash
# Run with a specific script
./burwell.exe --script path/to/script.json

# Run with custom configuration
./burwell.exe --config path/to/config.json --script path/to/script.json
```

## Script System

Burwell uses JSON-based scripts to define automation sequences. Scripts support variables, conditional logic, loops, and nested script execution.

### Basic Script Structure

```json
{
  "name": "Script Name",
  "description": "What this script does",
  "variables": {
    "var1": "initial value",
    "var2": 123
  },
  "commands": [
    {
      "command": "COMMAND_NAME",
      "parameters": {
        "param1": "value1",
        "param2": "value2"
      },
      "result_variable": "optional_result_var"
    }
  ]
}
```

## Command Reference

### Variable Commands

#### SET_VARIABLE
Sets a variable in the execution context.

**Parameters:**
- `name` (required): Variable name
- `value` (required): Variable value (any type)

**Example:**
```json
{
  "command": "SET_VARIABLE",
  "parameters": {
    "name": "username",
    "value": "john_doe"
  }
}
```

#### GET_VARIABLE
Retrieves a variable value from the execution context.

**Parameters:**
- `name` (required): Variable name to retrieve

**Example:**
```json
{
  "command": "GET_VARIABLE",
  "parameters": {
    "name": "username"
  },
  "result_variable": "retrieved_username"
}
```

## Conditional Commands

All conditional commands perform case-insensitive comparisons and return "true" or "false".

### IF_CONTAINS
Checks if a variable contains a substring.

**Parameters:**
- `variable` (required): Variable name to check
- `substring` (required): Substring to search for
- `store_as` (optional): Variable name to store the boolean result

**Example:**
```json
{
  "command": "IF_CONTAINS",
  "parameters": {
    "variable": "window_title",
    "substring": "Chrome",
    "store_as": "has_chrome"
  }
}
```

### IF_NOT_CONTAINS
Checks if a variable does NOT contain a substring.

**Parameters:**
- `variable` (required): Variable name to check
- `substring` (required): Substring to search for
- `store_as` (optional): Variable name to store the boolean result

**Example:**
```json
{
  "command": "IF_NOT_CONTAINS",
  "parameters": {
    "variable": "edgeWindows",
    "substring": "deepseek",
    "store_as": "no_deepseek_found"
  }
}
```

### IF_EQUALS
Checks if a variable equals a specific value.

**Parameters:**
- `variable` (required): Variable name to check
- `value` (required): Value to compare against
- `store_as` (optional): Variable name to store the boolean result

**Example:**
```json
{
  "command": "IF_EQUALS",
  "parameters": {
    "variable": "status",
    "value": "active",
    "store_as": "is_active"
  }
}
```

### IF_NOT_EQUALS
Checks if a variable does NOT equal a specific value.

**Parameters:**
- `variable` (required): Variable name to check
- `value` (required): Value to compare against
- `store_as` (optional): Variable name to store the boolean result

**Example:**
```json
{
  "command": "IF_NOT_EQUALS",
  "parameters": {
    "variable": "browser",
    "value": "firefox",
    "store_as": "not_firefox"
  }
}
```

## Control Flow Commands

### EXECUTE_SCRIPT
Executes another script file, enabling script composition and reusability.

**Parameters:**
- `script_path` (required): Path to the script file to execute
- `result_variable` (optional): Variable to store the execution result
- `continue_on_failure` (optional): Boolean to continue if script fails

**Example:**
```json
{
  "command": "EXECUTE_SCRIPT",
  "parameters": {
    "script_path": "test_scripts/minimize_all_wins.json",
    "result_variable": "minimize_result",
    "continue_on_failure": false
  }
}
```

### WHILE_LOOP
Executes a sequence of commands while a condition is true.

**Parameters:**
- `condition_variable` (required): Variable containing boolean condition
- `max_iterations` (optional): Maximum number of iterations (default: 100)
- `commands` (required): Array of commands to execute in the loop

**Example:**
```json
{
  "command": "WHILE_LOOP",
  "parameters": {
    "condition_variable": "continue_searching",
    "max_iterations": 10,
    "commands": [
      {
        "command": "UIA_ENUM_WINDOWS",
        "parameters": {},
        "result_variable": "windows"
      },
      {
        "command": "IF_CONTAINS",
        "parameters": {
          "variable": "windows",
          "substring": "target_app",
          "store_as": "continue_searching"
        }
      }
    ]
  }
}
```

### CONDITIONAL_STOP
Stops script execution if a condition is true. Useful for early exit from scripts based on conditions.

**Parameters:**
- `condition_variable` (required): Variable name containing the boolean condition
- `invert` (optional): If true, stops when condition is false instead of true (default: false)

**Example:**
```json
{
  "command": "CONDITIONAL_STOP",
  "parameters": {
    "condition_variable": "error_occurred",
    "invert": false
  }
}
```

### WAIT
Pauses execution for a specified duration.

**Parameters:**
- `duration_ms` (required): Wait duration in milliseconds

**Example:**
```json
{
  "command": "WAIT",
  "parameters": {
    "duration_ms": 1000
  }
}
```

## UIA Commands

User Interface Automation (UIA) commands provide low-level control over Windows applications.

### Atomic UIA Operations

#### UIA_KEY_PRESS
Presses a key down (without releasing).

**Parameters:**
- `key` (required): Virtual key code or key name (e.g., "VK_LWIN", "VK_CONTROL", "A")

**Example:**
```json
{
  "command": "UIA_KEY_PRESS",
  "parameters": {
    "key": "VK_CONTROL"
  }
}
```

#### UIA_KEY_RELEASE
Releases a previously pressed key.

**Parameters:**
- `key` (required): Virtual key code or key name

**Example:**
```json
{
  "command": "UIA_KEY_RELEASE",
  "parameters": {
    "key": "VK_CONTROL"
  }
}
```

#### UIA_MOUSE_CLICK
Clicks the mouse at current position.

**Parameters:**
- `button` (optional): "LEFT", "RIGHT", or "MIDDLE" (default: "LEFT")
- `x` (optional): X coordinate for click position
- `y` (optional): Y coordinate for click position

**Example:**
```json
{
  "command": "UIA_MOUSE_CLICK",
  "parameters": {
    "button": "LEFT",
    "x": 500,
    "y": 300
  }
}
```

#### UIA_MOUSE_MOVE
Moves the mouse to specified coordinates.

**Parameters:**
- `x` (required): X coordinate
- `y` (required): Y coordinate

**Example:**
```json
{
  "command": "UIA_MOUSE_MOVE",
  "parameters": {
    "x": 100,
    "y": 200
  }
}
```

#### UIA_ENUM_WINDOWS
Enumerates all open windows.

**Parameters:**
- `filter` (optional): Filter windows by title pattern

**Result:**
Returns a JSON array of window information.

**Example:**
```json
{
  "command": "UIA_ENUM_WINDOWS",
  "parameters": {
    "filter": "*Edge*"
  },
  "result_variable": "edgeWindows"
}
```

#### UIA_FOCUS_WINDOW
Brings a window to the foreground.

**Parameters:**
- `hwnd` (required): Window handle (from UIA_ENUM_WINDOWS)

**Example:**
```json
{
  "command": "UIA_FOCUS_WINDOW",
  "parameters": {
    "hwnd": "0x00001234"
  }
}
```

#### UIA_GET_CLIPBOARD
Gets the current clipboard text content.

**Result:**
Returns the clipboard text content.

**Example:**
```json
{
  "command": "UIA_GET_CLIPBOARD",
  "parameters": {},
  "result_variable": "clipboard_content"
}
```

#### UIA_SET_CLIPBOARD
Sets the clipboard text content.

**Parameters:**
- `text` (required): Text to place on clipboard

**Example:**
```json
{
  "command": "UIA_SET_CLIPBOARD",
  "parameters": {
    "text": "Hello, World!"
  }
}
```

## Examples

### Example 1: Check if Chrome is Running
```json
{
  "name": "Check Chrome Status",
  "description": "Checks if Chrome browser is running",
  "variables": {},
  "commands": [
    {
      "command": "UIA_ENUM_WINDOWS",
      "parameters": {},
      "result_variable": "all_windows"
    },
    {
      "command": "IF_CONTAINS",
      "parameters": {
        "variable": "all_windows",
        "substring": "Chrome",
        "store_as": "chrome_running"
      }
    },
    {
      "command": "GET_VARIABLE",
      "parameters": {
        "name": "chrome_running"
      },
      "result_variable": "result"
    }
  ]
}
```

### Example 2: Conditional Window Focus
```json
{
  "name": "Focus Edge if No DeepSeek",
  "description": "Focuses Edge window only if DeepSeek is not found",
  "variables": {},
  "commands": [
    {
      "command": "UIA_ENUM_WINDOWS",
      "parameters": {
        "filter": "*Edge*"
      },
      "result_variable": "edgeWindows"
    },
    {
      "command": "IF_NOT_CONTAINS",
      "parameters": {
        "variable": "edgeWindows",
        "substring": "deepseek",
        "store_as": "should_focus"
      }
    },
    {
      "command": "IF_EQUALS",
      "parameters": {
        "variable": "should_focus",
        "value": "true"
      }
    }
  ]
}
```

### Example 3: Nested Script with Variables
```json
{
  "name": "Complex Automation",
  "description": "Demonstrates nested scripts and variable passing",
  "variables": {
    "search_term": "burwell"
  },
  "commands": [
    {
      "command": "EXECUTE_SCRIPT",
      "parameters": {
        "script_path": "scripts/minimize_all.json"
      }
    },
    {
      "command": "WAIT",
      "parameters": {
        "duration_ms": 500
      }
    },
    {
      "command": "SET_VARIABLE",
      "parameters": {
        "name": "browser_launched",
        "value": false
      }
    },
    {
      "command": "EXECUTE_SCRIPT",
      "parameters": {
        "script_path": "scripts/launch_browser.json",
        "result_variable": "launch_result",
        "continue_on_failure": true
      }
    }
  ]
}
```

## Best Practices

1. **Use Descriptive Names**: Give scripts and variables meaningful names
2. **Add Descriptions**: Document what each script does
3. **Handle Errors**: Use `continue_on_failure` for non-critical operations
4. **Test Incrementally**: Test scripts step by step before combining
5. **Reuse Scripts**: Use EXECUTE_SCRIPT to create modular automations
6. **Store Results**: Use result_variable to capture command outputs
7. **Check Conditions**: Use IF_* commands to make scripts adaptive

## Troubleshooting

### Common Issues

1. **"Unknown command type" Error**
   - Ensure command names are spelled correctly
   - Check that required parameters are provided

2. **Variable Not Found**
   - Initialize variables in the `variables` section
   - Check variable names for typos

3. **Script Not Found**
   - Use relative paths from the Burwell executable directory
   - Ensure script files have .json extension

4. **Window Not Found**
   - Use UIA_ENUM_WINDOWS to verify window titles
   - Window titles are case-sensitive for exact matches

### Debug Tips

- Set log level to DEBUG in configuration
- Use GET_VARIABLE to inspect variable values
- Test atomic commands individually before combining
- Check logs/burwell.log for detailed execution traces