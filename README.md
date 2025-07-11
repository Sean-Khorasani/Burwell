# Burwell

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![CMake](https://img.shields.io/badge/CMake-3.10%2B-green.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-blue.svg)](https://www.microsoft.com/windows)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

Burwell is an AI-powered desktop automation agent that started as an advanced QA testing tool but evolved into something far more powerful - a local AI assistant capable of performing any task a user can do on their system, autonomously and intelligently. Built with modern C++17, Burwell learns from your workflows, abstracts solutions into reusable skills, and continuously improves itself. It currently provides a secure, scalable platform for Windows automation, with Linux and macOS support planned for future releases.

### What Makes Burwell Special

Originally designed for automated GUI testing, Burwell's AI-driven approach revealed its potential as a universal desktop assistant. Unlike traditional automation tools that require rigid scripts, Burwell understands context, adapts to changes, and can figure out new tasks on its own - making it perfect for both QA professionals and everyday users.

### Real-World Scenarios

**🔵 Advanced Desktop GUI Testing (Where Web Tools Can't Reach)**

*Complex Desktop Application Testing*
- *Scenario*: Testing a professional video editing suite (like Adobe Premiere) with GPU rendering, multiple windows, and system integration
- *Burwell Solution*: Navigates complex desktop GUI with nested menus, floating panels, and context-sensitive toolbars. Simultaneously monitors GPU usage during rendering, validates memory management during 4K video processing, tracks temp file creation/cleanup, and correlates frame drops with system resource spikes. Tests drag-and-drop between windows, custom keyboard shortcuts, and right-click context menus - things web automation tools simply cannot do

*Enterprise Desktop Software Integration Testing*
- *Scenario*: Testing a CAD/CAM application (like AutoCAD) integrated with PDM systems, license servers, and cloud storage
- *Burwell Solution*: Handles complex desktop GUI interactions like ribbon interfaces, property panels, and viewport manipulations while monitoring network calls to license servers, file locking mechanisms, and inter-process communication with plugins. Tests native file dialogs, system tray interactions, and Windows notifications. Validates that memory-mapped files are properly handled during large assembly loads

*Multi-Application Workflow Testing*
- *Scenario*: Testing a financial trading platform that integrates with Excel, Bloomberg Terminal, and proprietary risk management tools
- *Burwell Solution*: Orchestrates actions across multiple desktop applications - copies data from Bloomberg, pastes into Excel with special formatting, triggers VBA macros, then validates results in the trading GUI. Monitors DDE links, COM interactions, and clipboard operations. Tests alt-tab workflows, Windows snap layouts, and multi-monitor setups while tracking system resources and inter-process communications

*Native System Integration Testing*
- *Scenario*: Testing a security suite that includes kernel drivers, system services, and desktop GUI components
- *Burwell Solution*: Tests desktop GUI elements like system tray icons, toast notifications, and UAC elevation prompts while monitoring Windows Event Logs, service status, and driver load events. Validates context menu shell extensions, file system minifilter operations, and Windows Defender integration. Handles native Windows dialogs that web tools cannot even detect

**🔵 Traditional Intelligent QA Testing**
- *Scenario*: A QA engineer needs to test a web application across different browsers with various user flows
- *Burwell Solution*: Learns the application's UI patterns once, then autonomously navigates through test scenarios, adapting when UI elements change, and generating detailed reports with screenshots

**🔵 Personal Productivity Assistant**
- *Scenario*: Every morning, you check emails, update project boards, and compile daily reports from multiple sources
- *Burwell Solution*: Learns your routine, automatically opens applications, extracts relevant information, and prepares a consolidated morning briefing while you grab coffee

**🔵 Research Automation**
- *Scenario*: A researcher needs to collect data from various websites, academic databases, and local documents
- *Burwell Solution*: Navigates websites, handles authentication, extracts specific data points, and compiles everything into organized spreadsheets - even handling CAPTCHAs and dynamic content

**🔵 System Administration Helper**
- *Scenario*: IT admin needs to configure multiple Windows machines with specific settings and software
- *Burwell Solution*: Learns the configuration process once, then replicates it across machines, handling variations and errors intelligently

**🔵 Creative Workflow Automation**
- *Scenario*: A content creator needs to resize images, update metadata, and upload to multiple platforms
- *Burwell Solution*: Watches your workflow, learns your preferences, and automates the entire process while maintaining your creative standards

## 🔵 Key Features

- **Learn, Abstract, Reuse**: Learns from user requests, abstracts solutions into reusable tasks
- **Enterprise Security**: Encrypted credentials, comprehensive input validation, secure by design
- **High Performance**: Multi-threaded architecture with priority-based task scheduling
- **Extensible**: Plugin architecture with dependency injection
- **Windows Native**: Currently Windows 10/11 only, with cross-platform support planned

## 🔵 Quick Start

### Prerequisites

- Windows 10/11 (required)
- MSYS2
- Git

### Building from Source (Windows with MSYS2)

#### Step 1: Install MSYS2
1. Download MSYS2 from https://www.msys2.org/
2. Install to default location (C:\msys64)
3. Run MSYS2 and update the package database:
```bash
pacman -Syu
```

#### Step 2: Install Build Tools and Dependencies
Open **MSYS2 MinGW 64-bit** terminal and run:
```bash
# Install MinGW-w64 toolchain
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make

# Install required libraries
pacman -S mingw-w64-x86_64-nlohmann-json

# Install Git if not already installed
pacman -S git
```

#### Step 3: Clone and Build Burwell
Still in the **MSYS2 MinGW 64-bit** terminal:
```bash
# Clone the repository
git clone https://github.com/Sean-Khorasani/Burwell.git
cd Burwell

# Build using the provided script
./build.sh
```

The executable will be created at `build/bin/burwell.exe`.

**Important**: The resulting `burwell.exe` is fully statically linked and runs natively on any Windows 10/11 system without requiring MSYS2, MinGW, or any additional runtime libraries. It only depends on standard Windows system DLLs that are always present.

#### Alternative: Manual Build
If you prefer manual building:
```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

**Note**: Currently, Burwell only supports Windows. Linux and macOS support is planned for future releases.

### Running Burwell

```bash
./build/bin/burwell.exe
```

### Basic Usage

```bash
# Run with a specific automation script
burwell --script test_scripts/example_automation.json

# Interactive mode
burwell

# Example natural language commands
> Open Notepad and type Hello World
> Find all PDF files in Downloads folder
> Open browser and search for weather
```

### Script-Based Automation

Burwell uses JSON scripts for complex automation sequences. See the [User Guide](docs/user_guide.md) for complete script documentation and examples.

## 🔵 Documentation

- [User Guide](docs/user_guide.md) - Script system and command reference
- [Developer Guide](docs/developer_guide.md) - Architecture and development workflow
- [API Reference](docs/api_reference.md) - Complete API documentation
- [Coding Standards](docs/coding_standards.md) - Code style and best practices

## 🔵 Architecture

Burwell follows a modular architecture with clear separation of concerns:

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  UI Module  │────▶│ Orchestrator│────▶│ Task Engine │
└─────────────┘     └─────────────┘     └─────────────┘
                           │
                    ┌──────┴──────┐
                    ▼             ▼
            ┌─────────────┐ ┌─────────────┐
            │    OCAL     │ │ LLM Connect │
            └─────────────┘ └─────────────┘
```

### Core Components

- **Orchestrator**: Coordinates all operations and manages execution flow
- **Task Engine**: Manages and executes automation tasks
- **OCAL**: OS Control Abstraction Layer for Windows operations
- **LLM Connector**: Integrates with language models for intelligent automation
- **State Manager**: Thread-safe state and context management

## 🔵 Configuration

Configuration files are located in the `config/` directory:

- `burwell.json` - Main configuration file
- `commands.json` - Command definitions
- `tasks/` - Task script definitions

Example configuration:

```json
{
    "orchestrator": {
        "max_script_nesting_level": 3,
        "enable_user_confirmation": true
    },
    "services": {
        "useThreadSafeStateManager": true,
        "threadPoolSize": 0,
        "llmProvider": "default"
    }
}
```

## 🔵 Testing

Run the test suite:

```bash
# Run all tests
ctest --test-dir build

# Run specific test
./build/bin/test_threading.exe
./build/bin/test_dependency_injection.exe
```

## 🔵 Security

Burwell implements multiple security layers:

- **Encrypted Credentials**: Using Windows DPAPI for secure storage
- **Input Validation**: Comprehensive validation of all user inputs
- **RAII Pattern**: Automatic resource management prevents leaks
- **Sandboxing**: Commands run with minimum required privileges

For security vulnerabilities, please email shahin@resket.ca with details.

## 🔵 Contributing

We welcome contributions! Please fork the repository and submit pull requests.

### Development Setup

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Code Style

We use clang-format for code formatting:

```bash
clang-format -i src/**/*.cpp src/**/*.h
```


## 🔵 Roadmap

### Version 2.0 (Next Release - Q2 2025)
- [ ] **Modern GUI Interface**: Chat-based interface for natural interaction with Burwell
- [ ] **Visual Workflow Designer**: Drag-and-drop interface to create/edit automation workflows without coding
- [ ] **Live Automation Recorder**: Record your actions and let Burwell learn and generalize them
- [ ] **Plugin Architecture**: Allow third-party extensions for industry-specific testing (game testing, medical software, etc.)

### Version 2.5 (Q4 2025)
- [ ] **Local Model Training**: Train small, specialized models for repetitive tasks (OCR for specific UIs, custom element detection)
- [ ] **Multi-Agent Collaboration**: Multiple Burwell instances working together on complex test suites
- [ ] **Remote Desktop Control**: Control and test applications on remote machines via RDP/VNC
- [ ] **Advanced Debugging Tools**: Step-through automation execution, breakpoints, variable inspection

### Version 3.0 (2026)
- [ ] **Cross-Platform Support**: Native Linux and macOS implementations
- [ ] **Hybrid Cloud Execution**: Run heavy workloads in cloud while maintaining local control
- [ ] **Voice Command Integration**: "Hey Burwell, test the login flow with 100 random users"
- [ ] **AI-Powered Test Generation**: Automatically generate comprehensive test cases from application analysis

## 🔵 License

This project is licensed under the MIT License. For commercial use, please contact the author at shahin@resket.ca.

## 🔵 Acknowledgments

- Windows API documentation and examples
- nlohmann/json for JSON parsing
- The C++ community for best practices and guidelines

## 🔵 Support

- **Issues**: [GitHub Issues](https://github.com/Sean-Khorasani/Burwell/issues)
- **Email**: shahin@resket.ca

---

**Note**: Burwell is under active development. APIs may change between versions.