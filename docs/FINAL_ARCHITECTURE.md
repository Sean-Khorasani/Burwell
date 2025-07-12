# Burwell Final Architecture Documentation

## Executive Summary

Burwell is an AI-powered desktop automation agent with a hybrid architecture that solves Windows Session 0 isolation challenges while providing modern web-based management capabilities. The system uses a Service + Worker pattern with integrated web server for local and remote management.

## Core Architecture Decisions

### 1. Session 0 Isolation Solution
**Problem**: Windows services (Session 0) cannot perform UI automation on user desktop (Session 1+)
**Solution**: Hybrid Service + Worker architecture where service coordinates and worker executes

### 2. Communication Strategy  
**Decision**: Service-centric bidirectional TCP hub with integrated Mongoose web server
**Rationale**: Handles NAT traversal, provides persistent remote connectivity, and enables modern web UI

### 3. Web Server Technology
**Decision**: Mongoose embedded web server
**Rationale**: Designed for embedded systems, excellent React SPA support, minimal footprint, production-proven

## High-Level System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        INTERNET                                 │
└─────────────────────┬───────────────────────────────────────────┘
                      │
┌─────────────────────▼───────────────────────────────────────────┐
│                 Remote Server                                   │
│               (External Web UI)                                 │
│              Public IP: x.x.x.x                                │
└─────────────────────┬───────────────────────────────────────────┘
                      │ WebSocket (Outbound)
                      │ wss://remote-server.com:443/burwell
┌─────────────────────▼───────────────────────────────────────────┐
│                 NAT/Firewall                                    │
└─────────────────────┬───────────────────────────────────────────┘
                      │
┌─────────────────────▼───────────────────────────────────────────┐
│              Local Network                                      │
│              192.168.1.x                                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                BURWELL SERVICE                          │    │
│  │                 (Session 0)                             │    │
│  │                                                         │    │
│  │  ┌─────────────┐  ┌─────────────────────────────────┐   │    │
│  │  │   Remote    │  │      Mongoose Web Server       │   │    │
│  │  │ WebSocket   │  │        (Port 8080)             │   │    │
│  │  │   Client    │  │                                 │   │    │
│  │  │             │  │ • Static Files (React WebUI)   │   │    │
│  │  │ • Send      │  │ • REST API (/api/*)            │   │    │
│  │  │   Status    │  │ • WebSocket (/ws)              │   │    │
│  │  │ • Receive   │  │ • Authentication               │   │    │
│  │  │   Commands  │  │ • CORS Support                 │   │    │
│  │  │ • Heartbeat │  │                                 │   │    │
│  │  └─────────────┘  └─────────────────────────────────┘   │    │
│  │                                                         │    │
│  │  ┌─────────────────────────────────────────────────────┐   │    │
│  │  │            Task Queue & Orchestrator               │   │    │
│  │  │ • Message routing                                  │   │    │
│  │  │ • Task coordination                                │   │    │
│  │  │ • Configuration management                         │   │    │
│  │  │ • Logging coordination                             │   │    │
│  │  └─────────────────────────────────────────────────────┘   │    │
│  └─────────────────────┬───────────────────────────────────────┘    │
│                        │ Named Pipe                                │
│                        │ (IPC)                                     │
│  ┌─────────────────────▼───────────────────────────────────────┐    │
│  │                BURWELL WORKER                               │    │
│  │                 (Session 1+)                                │    │
│  │                                                             │    │
│  │  ┌─────────────────────────────────────────────────────┐    │    │
│  │  │            UI AUTOMATION ENGINE                     │    │    │
│  │  │                                                     │    │    │
│  │  │ • UIA Commands (Windows UI Automation)             │    │    │
│  │  │ • Mouse/Keyboard Control                            │    │    │
│  │  │ • Window Management                                 │    │    │
│  │  │ • OCR & Screen Capture                             │    │    │
│  │  │ • Browser Automation                               │    │    │
│  │  │ • File Operations                                  │    │    │
│  │  │ • Process Management                               │    │    │
│  │  └─────────────────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │              Local Clients                              │    │
│  │                                                         │    │
│  │ • Admin Browser (localhost:8080)                       │    │
│  │ • Future GUI Applications                              │    │
│  │ • API Integration Tools                                │    │
│  │ • Mobile Apps (same network)                          │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

## Component Interaction Flow

### Message Flow Patterns

**1. Remote Command Execution:**
```
Remote Server → Service (WebSocket) → Task Queue → Worker (Named Pipe) → UI Automation → Results → Service → Remote Server
```

**2. Local Web UI Interaction:**
```
Browser → Service (HTTP/REST) → Task Queue → Worker (Named Pipe) → UI Automation → Results → Service (WebSocket) → Browser
```

**3. Status Monitoring:**
```
Worker → Service (Named Pipe) → Both (Remote WebSocket + Local WebSocket) → Real-time Updates
```

## Detailed Component Architecture

### Service Component (Session 0)

**Responsibilities:**
- Network communication hub (inbound/outbound)
- Task queue management and coordination
- Configuration and security management
- Process lifecycle management (worker creation/monitoring)
- Logging coordination
- Authentication and authorization

**Core Modules:**
```cpp
class BurwellService {
    MongooseWebServer webServer;         // Embedded web server
    RemoteServerClient remoteClient;     // WebSocket client
    TaskQueue taskQueue;                 // Inter-process task queue
    WorkerManager workerManager;         // Process management
    ConfigManager configManager;         // Configuration
    SecurityManager securityManager;     // Authentication
};
```

### Worker Component (Session 1+)

**Responsibilities:**
- UI automation execution
- Direct user session interaction
- Task result generation
- Status reporting to service

**Core Modules:**
```cpp
class BurwellWorker {
    UIAutomationEngine uiaEngine;        // Windows UIA
    InputController inputController;     // Mouse/Keyboard
    WindowManager windowManager;         // Window operations
    OCREngine ocrEngine;                // Screen text extraction
    TaskExecutor taskExecutor;          // Automation logic
    ServiceCommunicator serviceCom;     // Named pipe client
};
```

### Web Server Component (Mongoose)

**Responsibilities:**
- Serve React WebUI static files
- Provide REST API endpoints
- Handle WebSocket real-time communication
- Manage authentication sessions
- Handle CORS for cross-origin requests

**API Endpoints:**
```
Static Files:
GET  /                    # React app (index.html)
GET  /static/*           # JS/CSS/images

REST API:
GET  /api/status         # System status
POST /api/tasks          # Execute task
GET  /api/tasks          # Task history
GET  /api/config         # Configuration
PUT  /api/config         # Update configuration
GET  /api/logs           # System logs
GET  /api/scripts        # Automation scripts
POST /api/scripts        # Create/update scripts
DELETE /api/scripts/:id  # Delete script

Real-time:
WebSocket /ws            # Live status updates
```

## Process Lifecycle Management

### Service Startup Sequence

1. **Windows Service Registration**
   - `StartServiceCtrlDispatcherA()` 
   - `ServiceMain()` entry point
   - Service status reporting

2. **Component Initialization**
   - Configuration loading
   - Logging system setup
   - Task queue initialization
   - Mongoose web server startup
   - Remote WebSocket client connection

3. **Worker Process Management**
   - Detect active user session (`WTSGetActiveConsoleSessionId()`)
   - Create user token (`OpenProcessToken()`, `DuplicateTokenEx()`)
   - Launch worker in user session (`CreateProcessAsUser()`)
   - Establish Named Pipe communication

4. **Operational State**
   - Service main loop
   - Task coordination
   - Health monitoring
   - Automatic worker restart on failure

### Worker Startup Sequence

1. **Session Context Validation**
   - Verify running in user session (Session 1+)
   - Initialize UI automation components
   - Connect to service via Named Pipe

2. **Automation Engine Initialization**
   - Windows UIA initialization
   - Input system setup
   - Window management preparation
   - OCR engine loading

3. **Service Registration**
   - Register with service as available worker
   - Begin task polling loop
   - Status reporting to service

## Communication Protocols

### Named Pipe Protocol (Service ↔ Worker)

**Message Format (JSON):**
```json
{
  "type": "task_request|task_response|status_update|heartbeat",
  "id": "unique_message_id",
  "timestamp": "2025-01-01T00:00:00Z",
  "payload": {
    // Task-specific data
  }
}
```

**Task Request Example:**
```json
{
  "type": "task_request",
  "id": "task_001",
  "timestamp": "2025-01-01T12:00:00Z",
  "payload": {
    "command": "UIA_CLICK",
    "parameters": {
      "x": 100,
      "y": 200,
      "button": "left"
    },
    "timeout": 5000
  }
}
```

### REST API Protocol

**Standard HTTP Response Format:**
```json
{
  "success": true|false,
  "data": {},
  "error": {
    "code": "ERROR_CODE",
    "message": "Human readable error"
  },
  "timestamp": "2025-01-01T00:00:00Z"
}
```

### WebSocket Protocol

**Real-time Event Format:**
```json
{
  "event": "status_update|task_complete|error|heartbeat",
  "timestamp": "2025-01-01T00:00:00Z",
  "data": {}
}
```

## Security Architecture

### Authentication & Authorization

**Service Level Security:**
- Windows Service runs as LocalSystem (required for CreateProcessAsUser)
- Worker runs with user privileges (Session 1+)
- Named Pipe uses Windows ACLs for access control

**Web Server Security:**
- HTTPS/TLS for all external communication
- JWT tokens for API authentication
- Session-based authentication for WebUI
- CORS policy for cross-origin protection
- Rate limiting on API endpoints

**Remote Connection Security:**
- WSS (WebSocket Secure) to remote server
- Certificate-based authentication
- Encrypted payload transmission
- Heartbeat mechanism for connection monitoring

### Data Protection

**Configuration Security:**
- Sensitive settings encrypted using Windows DPAPI
- API keys stored in secure Windows credential store
- Configuration file access restricted to service account

**Logging Security:**
- Structured logging with sanitized output
- No sensitive data in log files
- Log rotation with secure deletion
- Audit trail for all administrative actions

## Technology Stack

### Core Technologies
- **Language**: C++17
- **Build System**: CMake
- **JSON Processing**: nlohmann/json
- **Logging**: Custom structured logger
- **Web Server**: Mongoose (embedded)
- **UI Automation**: Windows UIA API
- **Process Communication**: Windows Named Pipes

### Frontend Technologies
- **Framework**: React 18 + TypeScript
- **Build Tool**: Vite/Webpack
- **UI Components**: Material-UI or Ant Design
- **State Management**: Redux Toolkit or Zustand
- **HTTP Client**: Axios
- **WebSocket**: Native WebSocket API
- **Code Editor**: Monaco Editor (VS Code engine)

### Development Tools
- **Compiler**: MinGW-w64 (cross-compilation from Linux)
- **Version Control**: Git
- **Documentation**: Markdown + UML diagrams
- **Testing**: Custom C++ test framework

## Deployment Architecture

### File Structure
```
burwell/
├── bin/
│   ├── burwell.exe              # Main service + worker
│   ├── burwell-service.exe      # Service installer/manager
│   └── web_assets/              # Embedded React WebUI
│       ├── index.html
│       ├── static/
│       │   ├── js/main.js
│       │   └── css/main.css
│       └── manifest.json
├── config/
│   ├── burwell.json             # Main configuration
│   ├── cpl/
│   │   └── commands.json        # Command definitions
│   └── llm_providers/           # LLM provider configs
├── logs/                        # Log files
├── scripts/                     # Automation scripts
└── temp/                        # Temporary files
```

### Installation Process
1. **Service Installation**
   ```cmd
   burwell-service.exe install burwell.exe
   ```

2. **Service Management**
   ```cmd
   # Start service
   net start BurwellAgent
   
   # Stop service
   net stop BurwellAgent
   
   # Check status
   sc query BurwellAgent
   ```

3. **Web UI Access**
   - Local: `https://localhost:8080`
   - Network: `https://[machine-ip]:8080`

## Development Workflow

### Build Process
```bash
# 1. Build React WebUI
cd web_ui/
npm run build
cp -r build/* ../src/web_assets/

# 2. Cross-compile C++ service
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw.cmake
cmake --build .

# 3. Package deployment
cp build/bin/* deployment/bin/
```

### Testing Strategy
- **Unit Tests**: Core C++ functionality
- **Integration Tests**: Service ↔ Worker communication
- **API Tests**: REST endpoint validation
- **UI Tests**: React component testing
- **System Tests**: End-to-end automation workflows

## Performance Considerations

### Resource Usage
- **Service Memory**: ~50-100MB baseline
- **Worker Memory**: ~30-50MB during automation
- **Network**: Minimal bandwidth (<1KB/s monitoring)
- **CPU**: Low usage except during active automation

### Scalability
- **Single Worker**: Handles sequential task execution
- **Future**: Multiple workers for parallel execution
- **Remote**: Service can manage multiple machine connections
- **Web UI**: Supports multiple concurrent admin sessions

## Future Enhancements

### Phase 1: Core Stability (Q1 2025)
- Complete Service + Worker implementation
- Basic React WebUI with task execution
- Remote server connectivity
- Production deployment testing

### Phase 2: Advanced Features (Q2 2025)
- Visual workflow designer in WebUI
- Advanced task scheduling
- Multi-user support with role-based access
- Mobile-responsive WebUI

### Phase 3: Enterprise Features (Q3 2025)
- Multiple worker support
- Load balancing and failover
- Advanced analytics and reporting
- API rate limiting and quotas

### Phase 4: AI Integration (Q4 2025)
- LLM-powered task generation
- Intelligent error recovery
- Natural language task description
- Machine learning for optimization

## Conclusion

This architecture provides a robust, scalable, and modern solution for desktop automation that addresses the key challenges of Windows Session 0 isolation while providing enterprise-grade management capabilities through a modern web interface. The hybrid approach balances security, performance, and usability requirements.