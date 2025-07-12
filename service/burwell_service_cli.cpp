#include "service_manager.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>

void printUsage() {
    std::cout << "Burwell Service Manager - Manage Burwell as Windows Service\n";
    std::cout << "Usage: burwell-service <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  install <path>  Install Burwell as Windows service\n";
    std::cout << "                  <path> - Full path to burwell.exe\n";
    std::cout << "  uninstall       Uninstall Burwell service\n";
    std::cout << "  start           Start Burwell service\n";
    std::cout << "  stop            Stop Burwell service\n";
    std::cout << "  status          Show service status\n";
    std::cout << "  restart         Restart Burwell service\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --name <name>   Service name (default: BurwellAgent)\n";
    std::cout << "  --help          Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  burwell-service install C:\\burwell\\burwell.exe\n";
    std::cout << "  burwell-service start\n";
    std::cout << "  burwell-service status\n";
    std::cout << "  burwell-service stop\n";
    std::cout << "  burwell-service uninstall\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    std::string serviceName = "BurwellAgent";
    std::string burwellPath;

    // Parse arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--name" && i + 1 < argc) {
            serviceName = argv[++i];
        } else if (arg == "--help") {
            printUsage();
            return 0;
        } else if (command == "install" && burwellPath.empty()) {
            burwellPath = arg;
        }
    }

    burwell::ServiceManager manager;

    if (command == "install") {
        if (burwellPath.empty()) {
            std::cerr << "Error: Path to burwell.exe is required for install command\n";
            std::cerr << "Usage: burwell-service install <path-to-burwell.exe>\n";
            return 1;
        }

        std::cout << "Installing Burwell service...\n";
        std::cout << "Service name: " << serviceName << "\n";
        std::cout << "Burwell path: " << burwellPath << "\n";
        
        // Show absolute path that will be used
        try {
            std::string absPath = std::filesystem::absolute(burwellPath).string();
            std::cout << "Absolute path: " << absPath << "\n";
        } catch (...) {
            // Continue even if we can't show absolute path
        }

        if (manager.installService(burwellPath, serviceName)) {
            std::cout << "Success: Burwell service installed successfully!\n";
            std::cout << "The service is configured to start automatically with Windows.\n";
            std::cout << "You can now manage the service using:\n";
            std::cout << "  burwell-service start/stop/status\n";
            std::cout << "  net start BurwellAgent\n";
            std::cout << "  net stop BurwellAgent\n";
            std::cout << "  sc query BurwellAgent\n";
            return 0;
        } else {
            std::cerr << "Error: Failed to install service\n";
            std::cerr << "Details: " << manager.getLastError() << "\n";
            return 1;
        }
    }
    
    else if (command == "uninstall") {
        std::cout << "Uninstalling Burwell service...\n";

        if (manager.uninstallService(serviceName)) {
            std::cout << "Success: Burwell service uninstalled successfully!\n";
            return 0;
        } else {
            std::cerr << "Error: Failed to uninstall service\n";
            std::cerr << "Details: " << manager.getLastError() << "\n";
            return 1;
        }
    }
    
    else if (command == "start") {
        std::cout << "Starting Burwell service...\n";

        if (manager.startService(serviceName)) {
            std::cout << "Success: Burwell service started successfully!\n";
            std::cout << "Status: " << manager.getServiceStatus(serviceName) << "\n";
            return 0;
        } else {
            std::cerr << "Error: Failed to start service\n";
            std::cerr << "Details: " << manager.getLastError() << "\n";
            return 1;
        }
    }
    
    else if (command == "stop") {
        std::cout << "Stopping Burwell service...\n";

        if (manager.stopService(serviceName)) {
            std::cout << "Success: Burwell service stopped successfully!\n";
            std::cout << "Status: " << manager.getServiceStatus(serviceName) << "\n";
            return 0;
        } else {
            std::cerr << "Error: Failed to stop service\n";
            std::cerr << "Details: " << manager.getLastError() << "\n";
            return 1;
        }
    }
    
    else if (command == "status") {
        std::string status = manager.getServiceStatus(serviceName);
        std::cout << "Service: " << serviceName << "\n";
        std::cout << "Status: " << status << "\n";

        if (status == "NOT_INSTALLED") {
            std::cout << "The service is not installed. Use 'burwell-service install <path>' to install it.\n";
        } else if (status == "RUNNING") {
            std::cout << "The service is running normally.\n";
        } else if (status == "STOPPED") {
            std::cout << "The service is stopped. Use 'burwell-service start' to start it.\n";
        }

        return 0;
    }
    
    else if (command == "restart") {
        std::cout << "Restarting Burwell service...\n";

        // Stop first
        std::cout << "Stopping service...\n";
        if (!manager.stopService(serviceName)) {
            std::cerr << "Warning: Failed to stop service: " << manager.getLastError() << "\n";
        }

        // Start
        std::cout << "Starting service...\n";
        if (manager.startService(serviceName)) {
            std::cout << "Success: Burwell service restarted successfully!\n";
            std::cout << "Status: " << manager.getServiceStatus(serviceName) << "\n";
            return 0;
        } else {
            std::cerr << "Error: Failed to start service after stop\n";
            std::cerr << "Details: " << manager.getLastError() << "\n";
            return 1;
        }
    }
    
    else {
        std::cerr << "Error: Unknown command '" << command << "'\n\n";
        printUsage();
        return 1;
    }

    return 0;
}