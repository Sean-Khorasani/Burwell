#include <iostream>
#include <cassert>
#include "common/dependency_injection.h"
#include "common/service_factory.h"
#include "common/logger.h"

using namespace burwell;

// Example interfaces and implementations for testing
class IMessageService {
public:
    virtual ~IMessageService() = default;
    virtual std::string getMessage() const = 0;
};

class ConsoleMessageService : public IMessageService {
public:
    std::string getMessage() const override {
        return "Message from console service";
    }
};

class INotificationService {
public:
    virtual ~INotificationService() = default;
    virtual void notify(const std::string& message) = 0;
};

class EmailNotificationService : public INotificationService {
    std::shared_ptr<IMessageService> m_messageService;
public:
    // Constructor injection
    explicit EmailNotificationService(std::shared_ptr<IMessageService> messageService)
        : m_messageService(messageService) {}
    
    void notify(const std::string& message) override {
        std::cout << "[EMAIL] " << message << " (using: " 
                  << m_messageService->getMessage() << ")\n";
    }
};

// Test basic DI container functionality
void testBasicDI() {
    std::cout << "\n[TEST] Basic Dependency Injection\n";
    
    DIContainer container;
    
    // Register services
    container.registerService<IMessageService, ConsoleMessageService>();
    
    // Register with factory
    container.registerFactory<INotificationService>([&container]() {
        auto messageService = container.resolve<IMessageService>();
        return std::make_shared<EmailNotificationService>(messageService);
    });
    
    // Resolve services
    auto notificationService = container.resolve<INotificationService>();
    notificationService->notify("Test notification");
    
    // Verify singleton behavior
    auto messageService1 = container.resolve<IMessageService>();
    auto messageService2 = container.resolve<IMessageService>();
    
    assert(messageService1 == messageService2);
    std::cout << "[PASS] Singleton services return same instance\n";
}

// Test transient services
void testTransientServices() {
    std::cout << "\n[TEST] Transient Services\n";
    
    DIContainer container;
    
    // Counter to track instantiations
    static int instanceCount = 0;
    
    class CountingService : public IMessageService {
        int m_id;
    public:
        CountingService() : m_id(++instanceCount) {}
        std::string getMessage() const override {
            return "Instance #" + std::to_string(m_id);
        }
    };
    
    // Register as transient
    container.registerService<IMessageService, CountingService>(
        DIContainer::Lifetime::TRANSIENT);
    
    // Each resolve creates new instance
    auto service1 = container.resolve<IMessageService>();
    auto service2 = container.resolve<IMessageService>();
    
    std::cout << "Service 1: " << service1->getMessage() << "\n";
    std::cout << "Service 2: " << service2->getMessage() << "\n";
    
    assert(service1 != service2);
    std::cout << "[PASS] Transient services create new instances\n";
}

// Test service factory with DI
void testServiceFactory() {
    std::cout << "\n[TEST] Service Factory Integration\n";
    
    ServiceFactory factory;
    
    // Initialize services (would normally load from config)
    factory.initializeServices("config/burwell.json");
    
    // Get container
    auto& container = factory.getContainer();
    
    // Test that we can create services through the factory
    std::cout << "Creating services through factory...\n";
    
    // Create orchestrator components
    auto executionEngine = factory.createExecutionEngine();
    auto eventManager = factory.createEventManager();
    
    assert(executionEngine != nullptr);
    assert(eventManager != nullptr);
    std::cout << "[PASS] Service factory creates components successfully\n";
}

// Test service locator pattern
void testServiceLocator() {
    std::cout << "\n[TEST] Service Locator Pattern\n";
    
    // Register globally
    ServiceLocator::registerService<IMessageService, ConsoleMessageService>();
    
    // Resolve from anywhere
    auto service = ServiceLocator::resolve<IMessageService>();
    std::cout << "Global service: " << service->getMessage() << "\n";
    
    std::cout << "[PASS] Service locator provides global access\n";
}

// Test complex dependency graphs
void testComplexDependencies() {
    std::cout << "\n[TEST] Complex Dependency Graphs\n";
    
    // Service A depends on B and C
    // Service B depends on D
    // Service C depends on D
    // Service D has no dependencies
    
    class IServiceA { public: virtual ~IServiceA() = default; virtual std::string getName() = 0; };
    class IServiceB { public: virtual ~IServiceB() = default; virtual std::string getName() = 0; };
    class IServiceC { public: virtual ~IServiceC() = default; virtual std::string getName() = 0; };
    class IServiceD { public: virtual ~IServiceD() = default; virtual std::string getName() = 0; };
    
    class ServiceD : public IServiceD {
    public:
        std::string getName() override { return "ServiceD"; }
    };
    
    class ServiceB : public IServiceB {
        std::shared_ptr<IServiceD> m_serviceD;
    public:
        explicit ServiceB(std::shared_ptr<IServiceD> serviceD) : m_serviceD(serviceD) {}
        std::string getName() override { 
            return "ServiceB(depends on " + m_serviceD->getName() + ")"; 
        }
    };
    
    class ServiceC : public IServiceC {
        std::shared_ptr<IServiceD> m_serviceD;
    public:
        explicit ServiceC(std::shared_ptr<IServiceD> serviceD) : m_serviceD(serviceD) {}
        std::string getName() override { 
            return "ServiceC(depends on " + m_serviceD->getName() + ")"; 
        }
    };
    
    class ServiceA : public IServiceA {
        std::shared_ptr<IServiceB> m_serviceB;
        std::shared_ptr<IServiceC> m_serviceC;
    public:
        ServiceA(std::shared_ptr<IServiceB> serviceB, std::shared_ptr<IServiceC> serviceC)
            : m_serviceB(serviceB), m_serviceC(serviceC) {}
        std::string getName() override { 
            return "ServiceA(depends on " + m_serviceB->getName() + 
                   " and " + m_serviceC->getName() + ")"; 
        }
    };
    
    DIContainer container;
    
    // Register in dependency order
    container.registerService<IServiceD, ServiceD>();
    
    container.registerFactory<IServiceB>([&container]() {
        return std::make_shared<ServiceB>(container.resolve<IServiceD>());
    });
    
    container.registerFactory<IServiceC>([&container]() {
        return std::make_shared<ServiceC>(container.resolve<IServiceD>());
    });
    
    container.registerFactory<IServiceA>([&container]() {
        return std::make_shared<ServiceA>(
            container.resolve<IServiceB>(),
            container.resolve<IServiceC>()
        );
    });
    
    // Resolve the root service
    auto serviceA = container.resolve<IServiceA>();
    std::cout << "Resolved: " << serviceA->getName() << "\n";
    
    std::cout << "[PASS] Complex dependency graph resolved successfully\n";
}

int main() {
    // Initialize logger
    Logger::setLogLevel(LogLevel::INFO);
    
    std::cout << "=== Burwell Dependency Injection Test ===\n";
    
    try {
        testBasicDI();
        testTransientServices();
        testServiceFactory();
        testServiceLocator();
        testComplexDependencies();
        
        std::cout << "\n[SUCCESS] All dependency injection tests passed!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Test failed: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}