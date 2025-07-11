# Burwell

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![CMake](https://img.shields.io/badge/CMake-3.10%2B-green.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-blue.svg)](https://www.microsoft.com/windows)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)

Burwell is an enterprise-grade desktop automation agent that learns, abstracts, and reuses solutions. Built with modern C++17, it currently provides a secure, scalable, and maintainable platform for Windows automation, with Linux and macOS support planned for future releases.

## 🌟 Key Features

- **Learn, Abstract, Reuse**: Learns from user requests, abstracts solutions into reusable tasks
- **Enterprise Security**: Encrypted credentials, comprehensive input validation, secure by design
- **High Performance**: Multi-threaded architecture with priority-based task scheduling
- **Extensible**: Plugin architecture with dependency injection
- **Windows Native**: Currently Windows 10/11 only, with cross-platform support planned

## 🚀 Quick Start

### Prerequisites

- Windows 10/11 (required)
- MSYS2 (download from https://www.msys2.org/)
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

## 📚 Documentation

- [User Guide](docs/user_guide.md) - Script system and command reference
- [Developer Guide](docs/developer_guide.md) - Architecture and development workflow
- [API Reference](docs/api_reference.md) - Complete API documentation
- [Coding Standards](docs/coding_standards.md) - Code style and best practices

## 🏗️ Architecture

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

## 🔧 Configuration

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

## 🧪 Testing

Run the test suite:

```bash
# Run all tests
ctest --test-dir build

# Run specific test
./build/bin/test_threading.exe
./build/bin/test_dependency_injection.exe
```

## 🛡️ Security

Burwell implements multiple security layers:

- **Encrypted Credentials**: Using Windows DPAPI for secure storage
- **Input Validation**: Comprehensive validation of all user inputs
- **RAII Pattern**: Automatic resource management prevents leaks
- **Sandboxing**: Commands run with minimum required privileges

For security vulnerabilities, please email shahin@resket.ca with details.

## 🤝 Contributing

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


## 🗺️ Roadmap

### Version 2.1 (Current)
- [x] Enterprise security framework
- [x] Thread-safe architecture
- [x] Dependency injection
- [x] Comprehensive documentation

### Version 2.2 (Planned)
- [ ] GUI interface (modern chat UI)
- [ ] Plugin system
- [ ] Remote control capabilities
- [ ] Machine learning integration

### Version 3.0 (Future)
- [ ] Cross-platform support (Linux, macOS)
- [ ] Cloud integration
- [ ] Natural language understanding
- [ ] Visual automation designer

## 📝 License

This project is licensed under the MIT License. For commercial use, please contact the author at shahin@resket.ca.

## 🙏 Acknowledgments

- Windows API documentation and examples
- nlohmann/json for JSON parsing
- The C++ community for best practices and guidelines

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/Sean-Khorasani/Burwell/issues)
- **Email**: shahin@resket.ca

---

**Note**: Burwell is under active development. APIs may change between versions.