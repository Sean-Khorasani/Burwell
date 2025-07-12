# Burwell Service Manager

The Burwell Service Manager allows you to install and manage Burwell as a Windows service, enabling it to run in the background without a console window.

## Features

- Install Burwell as a Windows service
- Uninstall the service
- Start, stop, and restart the service
- Check service status
- Automatic restart on failure
- Runs Burwell in daemon mode (no console output)

## Building

The service manager is built automatically when you build Burwell:

```bash
./build.sh
```

This creates `burwell-service.exe` in the `build/bin` directory.

## Usage

**Note**: All service operations require Administrator privileges. Run Command Prompt as Administrator.

### Install Service

```cmd
burwell-service install C:\path\to\burwell.exe
```

This will:
- Install Burwell as a Windows service named "BurwellAgent"
- Configure it to start automatically with Windows
- Set up automatic restart on failure
- Use the `--daemon` flag to run without console output

### Check Service Status

```cmd
burwell-service status
```

Possible statuses:
- `NOT_INSTALLED` - Service is not installed
- `STOPPED` - Service is installed but not running
- `RUNNING` - Service is running normally
- `START_PENDING` - Service is starting
- `STOP_PENDING` - Service is stopping

### Start Service

```cmd
burwell-service start
```

### Stop Service

```cmd
burwell-service stop
```

### Restart Service

```cmd
burwell-service restart
```

### Uninstall Service

```cmd
burwell-service uninstall
```

## Custom Service Name

You can specify a custom service name:

```cmd
burwell-service install C:\burwell\burwell.exe --name MyBurwellService
burwell-service start --name MyBurwellService
burwell-service status --name MyBurwellService
```

## Service Configuration

The installed service has the following configuration:

- **Service Name**: BurwellAgent (default)
- **Display Name**: Burwell AI Desktop Agent
- **Start Type**: Automatic (starts with Windows)
- **Account**: Local System
- **Failure Actions**: 
  - First failure: Restart after 5 seconds
  - Second failure: Restart after 10 seconds
  - Subsequent failures: No action
  - Reset failure count after 1 hour

## Daemon Mode

When running as a service, Burwell automatically uses daemon mode:

- Console window is hidden
- No console output
- Logging only to files (check `logs/burwell.log`)
- Suitable for background operation

## Logging

When running as a service, check the log files:

```
C:\path\to\burwell\logs\burwell.log
```

The service manager also logs its operations to the Windows Event Log under "Application".

## Troubleshooting

### Service Won't Start

1. Check that the path to `burwell.exe` is correct
2. Verify the executable has the necessary permissions
3. Check Windows Event Viewer for detailed error messages
4. Ensure all required DLLs are available (should not be needed after static linking fix)

### Permission Denied

- Ensure you're running as Administrator
- The service account (Local System) needs access to the Burwell directory

### Service Starts but Stops Immediately

- Check `logs/burwell.log` for error messages
- Verify configuration files are accessible
- Test running `burwell.exe --daemon` manually first

## Example Workflow

```cmd
# Run as Administrator
cd C:\burwell\build\bin

# Install the service
burwell-service.exe install burwell.exe

# Expected output:
# Installing Burwell service...
# Service name: BurwellAgent
# Burwell path: burwell.exe
# Success: Burwell service installed successfully!
# The service is configured to start automatically with Windows.
# Use 'burwell-service start' to start it now.

# Check it was installed
burwell-service.exe status
# Output: Service: BurwellAgent
#         Status: STOPPED

# Start the service
burwell-service.exe start
# Output: Starting Burwell service...
#         Success: Burwell service started successfully!
#         Status: RUNNING

# Verify it's running
burwell-service.exe status
# Output: Service: BurwellAgent
#         Status: RUNNING
#         The service is running normally.

# Alternative: Use Windows commands
net start BurwellAgent
net stop BurwellAgent
sc query BurwellAgent

# When done, stop and uninstall
burwell-service.exe stop
burwell-service.exe uninstall
```

## Integration with System

Once installed as a service:

- Burwell starts automatically when Windows boots
- Runs in the background without user interaction
- Can be managed through Windows Services console (`services.msc`)
- Appears in Task Manager as a running service
- Automatically restarts if it crashes