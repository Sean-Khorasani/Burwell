# Burwell UML Diagrams

## High-Level Component Diagram

```mermaid
graph TB
    subgraph "Internet"
        RS[Remote Server<br/>External Web UI<br/>Public IP]
    end
    
    subgraph "Local Network"
        subgraph "Session 0 (Service)"
            BS[Burwell Service]
            subgraph "BS Components"
                RC[Remote Client<br/>WebSocket]
                WS[Mongoose<br/>Web Server<br/>Port 8080]
                TQ[Task Queue<br/>Orchestrator]
                WM[Worker Manager<br/>Process Control]
            end
        end
        
        subgraph "Session 1+ (User)"
            BW[Burwell Worker]
            subgraph "BW Components"
                UIA[UI Automation<br/>Engine]
                IC[Input Controller<br/>Mouse/Keyboard]
                WMgr[Window Manager]
                OCR[OCR Engine]
            end
        end
        
        subgraph "Local Clients"
            BROWSER[Admin Browser<br/>localhost:8080]
            GUI[Future GUI Apps]
            API[API Tools]
        end
    end
    
    %% Connections
    RS -.->|WSS| RC
    RC <--> TQ
    WS <--> TQ
    TQ <-->|Named Pipe| BW
    BROWSER --> WS
    GUI --> WS
    API --> WS
    
    %% Styling
    classDef service fill:#e1f5fe
    classDef worker fill:#f3e5f5
    classDef client fill:#e8f5e8
    classDef remote fill:#fff3e0
    
    class BS,RC,WS,TQ,WM service
    class BW,UIA,IC,WMgr,OCR worker
    class BROWSER,GUI,API client
    class RS remote
```

## Class Diagram - Core Architecture

```mermaid
classDiagram
    class BurwellService {
        +MongooseWebServer webServer
        +RemoteServerClient remoteClient
        +TaskQueue taskQueue
        +WorkerManager workerManager
        +ConfigManager configManager
        +SecurityManager securityManager
        
        +start() void
        +stop() void
        +processTask(Task) TaskResult
        +manageWorker() void
    }
    
    class MongooseWebServer {
        +int port
        +mg_mgr manager
        +TaskQueue* taskQueue
        
        +start() void
        +stop() void
        +handleHttpRequest(mg_connection*, mg_http_message*) void
        +handleWebSocket(mg_connection*, mg_ws_message*) void
        +serveStaticFiles(string path) void
        +handleApiEndpoint(string endpoint, json data) json
    }
    
    class RemoteServerClient {
        +string serverUrl
        +WebSocketConnection connection
        +TaskQueue* taskQueue
        
        +connect() bool
        +disconnect() void
        +sendMessage(json message) void
        +receiveMessage() json
        +handleHeartbeat() void
    }
    
    class TaskQueue {
        +queue~Task~ pendingTasks
        +map~string,TaskResult~ completedTasks
        +mutex queueMutex
        
        +enqueueTask(Task) void
        +dequeueTask() Task
        +addResult(string taskId, TaskResult) void
        +getResult(string taskId) TaskResult
    }
    
    class WorkerManager {
        +HANDLE workerProcess
        +HANDLE namedPipe
        +bool workerConnected
        
        +createWorkerProcess() bool
        +killWorkerProcess() void
        +restartWorker() void
        +sendToWorker(json message) void
        +receiveFromWorker() json
    }
    
    class BurwellWorker {
        +UIAutomationEngine uiaEngine
        +InputController inputController
        +WindowManager windowManager
        +OCREngine ocrEngine
        +ServiceCommunicator serviceCom
        
        +initialize() bool
        +processTask(Task) TaskResult
        +reportStatus() void
        +shutdown() void
    }
    
    class UIAutomationEngine {
        +IUIAutomation* uiaClient
        +map~string,AutomationElement~ elementCache
        
        +findElement(ElementLocator) AutomationElement
        +clickElement(AutomationElement) bool
        +typeText(string text) bool
        +getElementText(AutomationElement) string
        +waitForElement(ElementLocator, int timeout) bool
    }
    
    class InputController {
        +sendMouseClick(int x, int y, MouseButton) void
        +sendKeyPress(VirtualKey) void
        +sendKeyCombo(vector~VirtualKey~) void
        +typeString(string) void
        +moveMouse(int x, int y) void
    }
    
    class Task {
        +string id
        +string command
        +json parameters
        +int timeout
        +DateTime createdAt
        +string source
    }
    
    class TaskResult {
        +string taskId
        +bool success
        +json data
        +string errorMessage
        +DateTime completedAt
        +int executionTimeMs
    }
    
    %% Relationships
    BurwellService *-- MongooseWebServer
    BurwellService *-- RemoteServerClient
    BurwellService *-- TaskQueue
    BurwellService *-- WorkerManager
    
    MongooseWebServer --> TaskQueue
    RemoteServerClient --> TaskQueue
    WorkerManager --> TaskQueue
    
    BurwellWorker *-- UIAutomationEngine
    BurwellWorker *-- InputController
    
    TaskQueue --> Task
    TaskQueue --> TaskResult
```

## Sequence Diagram - Task Execution Flow

```mermaid
sequenceDiagram
    participant RS as Remote Server
    participant RC as Remote Client
    participant TQ as Task Queue
    participant WS as Web Server
    participant WM as Worker Manager
    participant BW as Burwell Worker
    participant UIA as UI Automation
    participant Browser as Admin Browser
    
    Note over RS,Browser: Remote Task Execution Flow
    
    RS->>RC: WebSocket: Execute Task
    RC->>TQ: Enqueue Task
    TQ->>WM: Notify New Task
    WM->>BW: Named Pipe: Task Message
    BW->>UIA: Execute Automation
    UIA->>UIA: Perform UI Actions
    UIA->>BW: Return Result
    BW->>WM: Named Pipe: Task Result
    WM->>TQ: Store Result
    TQ->>RC: Task Completed
    RC->>RS: WebSocket: Task Result
    
    Note over RS,Browser: Local Web UI Task Execution
    
    Browser->>WS: HTTP POST /api/tasks
    WS->>TQ: Enqueue Task
    TQ->>WM: Notify New Task
    WM->>BW: Named Pipe: Task Message
    BW->>UIA: Execute Automation
    UIA->>UIA: Perform UI Actions
    UIA->>BW: Return Result
    BW->>WM: Named Pipe: Task Result
    WM->>TQ: Store Result
    TQ->>WS: Get Result
    WS->>Browser: HTTP Response: Task Result
    
    Note over RS,Browser: Real-time Status Updates
    
    BW->>WM: Status Update
    WM->>TQ: Update Status
    TQ->>WS: Broadcast Status
    WS->>Browser: WebSocket: Status Update
    TQ->>RC: Status Change
    RC->>RS: WebSocket: Status Update
```

## State Diagram - Service Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Installing
    Installing --> Stopped : Install Complete
    Installing --> [*] : Install Failed
    
    Stopped --> Starting : Start Command
    Starting --> Running : Startup Success
    Starting --> Stopped : Startup Failed
    
    Running --> WorkerConnecting : Initialize Worker
    WorkerConnecting --> Operational : Worker Connected
    WorkerConnecting --> Running : Worker Failed
    
    Operational --> Processing : Task Received
    Processing --> Operational : Task Complete
    Processing --> Error : Task Failed
    
    Error --> Operational : Error Resolved
    Error --> Stopping : Critical Error
    
    Operational --> Stopping : Stop Command
    Running --> Stopping : Stop Command
    Stopped --> Stopping : Stop Command
    
    Stopping --> Stopped : Shutdown Complete
    Stopped --> Uninstalling : Uninstall Command
    Uninstalling --> [*] : Uninstall Complete
    
    state Operational {
        [*] --> Idle
        Idle --> TaskQueued : New Task
        TaskQueued --> Executing : Worker Available
        Executing --> Idle : Task Complete
        Executing --> Failed : Execution Error
        Failed --> Idle : Error Handled
    }
```

## Deployment Diagram

```mermaid
graph TB
    subgraph "Windows Machine"
        subgraph "Disk Storage"
            EXE[burwell.exe<br/>Service + Worker]
            SVC[burwell-service.exe<br/>Service Manager]
            CFG[config/<br/>Configuration Files]
            WEB[web_assets/<br/>React WebUI]
            LOG[logs/<br/>Log Files]
            SCR[scripts/<br/>Automation Scripts]
        end
        
        subgraph "Session 0 (System)"
            SVCMGR[Windows Service<br/>Control Manager]
            BSVC[Burwell Service<br/>Process]
        end
        
        subgraph "Session 1+ (User)"
            BWORK[Burwell Worker<br/>Process]
            DESKTOP[User Desktop<br/>UI Elements]
        end
        
        subgraph "Network Stack"
            HTTP[HTTP Server<br/>Port 8080]
            WSS[WebSocket Client<br/>Outbound]
        end
    end
    
    subgraph "External"
        REMOTE[Remote Server<br/>External Management]
        CLIENT[Client Browser<br/>Local Admin]
    end
    
    %% File relationships
    SVCMGR --> BSVC
    BSVC --> EXE
    BSVC --> CFG
    BSVC --> LOG
    BSVC --> WEB
    
    %% Process relationships
    BSVC <--> BWORK
    BWORK --> DESKTOP
    BSVC --> HTTP
    BSVC --> WSS
    
    %% Network relationships
    HTTP <--> CLIENT
    WSS <--> REMOTE
    
    %% Script execution
    BWORK --> SCR
    
    %% Styling
    classDef storage fill:#f9f9f9
    classDef system fill:#e1f5fe
    classDef user fill:#f3e5f5
    classDef network fill:#e8f5e8
    classDef external fill:#fff3e0
    
    class EXE,SVC,CFG,WEB,LOG,SCR storage
    class SVCMGR,BSVC system
    class BWORK,DESKTOP user
    class HTTP,WSS network
    class REMOTE,CLIENT external
```

## Activity Diagram - Worker Process Creation

```mermaid
flowchart TD
    START([Service Startup]) --> CHECK{Active User<br/>Session?}
    
    CHECK -->|No| WAIT[Wait for User Logon]
    WAIT --> CHECK
    
    CHECK -->|Yes| GETSID[Get Active Session ID<br/>WTSGetActiveConsoleSessionId]
    
    GETSID --> GETTOKEN[Get Explorer Process Token<br/>OpenProcess + OpenProcessToken]
    
    GETTOKEN --> DUPTOKEN[Duplicate Token<br/>DuplicateTokenEx]
    
    DUPTOKEN --> SETID[Set Session ID in Token<br/>SetTokenInformation]
    
    SETID --> CREATEPIPE[Create Named Pipe<br/>For Communication]
    
    CREATEPIPE --> LAUNCH[Launch Worker Process<br/>CreateProcessAsUser]
    
    LAUNCH --> SUCCESS{Process<br/>Created?}
    
    SUCCESS -->|No| ERROR[Log Error<br/>Retry in 30s]
    ERROR --> GETSID
    
    SUCCESS -->|Yes| WAITCONN[Wait for Worker<br/>Named Pipe Connection]
    
    WAITCONN --> CONNECTED{Worker<br/>Connected?}
    
    CONNECTED -->|Timeout| KILL[Kill Worker Process]
    KILL --> ERROR
    
    CONNECTED -->|Yes| REGISTER[Register Worker<br/>Exchange Capabilities]
    
    REGISTER --> MONITOR[Monitor Worker<br/>Health & Status]
    
    MONITOR --> RUNNING{Worker<br/>Running?}
    
    RUNNING -->|Yes| MONITOR
    RUNNING -->|No| RESTART{Auto<br/>Restart?}
    
    RESTART -->|Yes| LAUNCH
    RESTART -->|No| END([Worker Offline])
```

## Component Integration Points

```mermaid
graph LR
    subgraph "Integration Points"
        A[Service Manager<br/>Install/Uninstall] 
        B[Windows Service<br/>SCM Integration]
        C[Named Pipe<br/>IPC Communication]
        D[Mongoose<br/>HTTP/WebSocket]
        E[Remote Client<br/>WebSocket Client]
        F[UI Automation<br/>Windows UIA]
        G[Configuration<br/>JSON + DPAPI]
        H[Logging<br/>Structured Logger]
    end
    
    A -->|Creates/Removes| B
    B -->|Launches| C
    C -->|Communicates| F
    B -->|Hosts| D
    B -->|Connects| E
    B -->|Uses| G
    B -->|Uses| H
    F -->|Reports to| C
    
    classDef integration fill:#e8f4fd,stroke:#1976d2
    class A,B,C,D,E,F,G,H integration
```