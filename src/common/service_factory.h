#ifndef BURWELL_SERVICE_FACTORY_H
#define BURWELL_SERVICE_FACTORY_H

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include "dependency_injection.h"
#include "structured_logger.h"

namespace burwell {

// Forward declarations
class CommandParser;
class LLMConnector;
class TaskEngine;
class OCAL;
class EnvironmentalPerception;
class UIModule;
class StateManager;
class ExecutionEngine;
class EventManager;
class ScriptManager;
class FeedbackController;
class ConversationManager;

/**
 * @brief Factory for creating Burwell services
 * 
 * Provides centralized creation of all major components with proper
 * dependency injection and configuration.
 */
class ServiceFactory {
public:
    ServiceFactory();
    ~ServiceFactory();
    
    /**
     * @brief Initialize all core services
     * @param configPath Path to configuration file
     */
    void initializeServices(const std::string& configPath);
    
    /**
     * @brief Create and register command parser
     */
    std::shared_ptr<CommandParser> createCommandParser();
    
    /**
     * @brief Create and register LLM connector
     * @param providerName LLM provider name from config
     */
    std::shared_ptr<LLMConnector> createLLMConnector(const std::string& providerName);
    
    /**
     * @brief Create and register task engine
     */
    std::shared_ptr<TaskEngine> createTaskEngine();
    
    /**
     * @brief Create and register OCAL
     */
    std::shared_ptr<OCAL> createOCAL();
    
    /**
     * @brief Create and register environmental perception
     */
    std::shared_ptr<EnvironmentalPerception> createEnvironmentalPerception();
    
    /**
     * @brief Create and register UI module
     * @param uiType UI type (console, gui, etc.)
     */
    std::shared_ptr<UIModule> createUIModule(const std::string& uiType);
    
    /**
     * @brief Create and register state manager
     * @param threadSafe Use thread-safe implementation
     */
    std::shared_ptr<StateManager> createStateManager(bool threadSafe = true);
    
    /**
     * @brief Create and register execution engine
     */
    std::shared_ptr<ExecutionEngine> createExecutionEngine();
    
    /**
     * @brief Create and register event manager
     */
    std::shared_ptr<EventManager> createEventManager();
    
    /**
     * @brief Create and register script manager
     */
    std::shared_ptr<ScriptManager> createScriptManager();
    
    /**
     * @brief Create and register feedback controller
     */
    std::shared_ptr<FeedbackController> createFeedbackController();
    
    /**
     * @brief Create and register conversation manager
     */
    std::shared_ptr<ConversationManager> createConversationManager();
    
    /**
     * @brief Get the dependency injection container
     */
    DIContainer& getContainer() { return m_container; }
    
    /**
     * @brief Register custom service creators
     */
    template<typename TInterface>
    void registerCreator(const std::string& name, 
                        std::function<std::shared_ptr<TInterface>()> creator) {
        m_creators[name] = [creator]() -> std::shared_ptr<void> {
            return creator();
        };
    }
    
    /**
     * @brief Create a service by name
     */
    template<typename TInterface>
    std::shared_ptr<TInterface> create(const std::string& name) {
        auto it = m_creators.find(name);
        if (it != m_creators.end()) {
            return std::static_pointer_cast<TInterface>(it->second());
        }
        
        SLOG_ERROR().message("Creator not found").context("name", name);
        return nullptr;
    }
    
private:
    DIContainer m_container;
    std::unordered_map<std::string, std::function<std::shared_ptr<void>()>> m_creators;
    
    // Configuration
    struct ServiceConfig {
        bool useThreadSafeStateManager = true;
        std::string llmProvider = "default";
        std::string uiType = "console";
        int threadPoolSize = 0;  // 0 = auto-detect
    };
    
    ServiceConfig m_config;
    
    void loadConfiguration(const std::string& configPath);
};

/**
 * @brief Abstract factory interface for extensibility
 */
template<typename T>
class IFactory {
public:
    virtual ~IFactory() = default;
    virtual std::shared_ptr<T> create() = 0;
    virtual std::string getName() const = 0;
};

/**
 * @brief Concrete factory implementation
 */
template<typename TInterface, typename TImplementation>
class ConcreteFactory : public IFactory<TInterface> {
public:
    explicit ConcreteFactory(const std::string& name) : m_name(name) {}
    
    std::shared_ptr<TInterface> create() override {
        return std::make_shared<TImplementation>();
    }
    
    std::string getName() const override {
        return m_name;
    }
    
private:
    std::string m_name;
};

/**
 * @brief Factory registry for managing multiple factories
 */
template<typename T>
class FactoryRegistry {
public:
    /**
     * @brief Register a factory
     */
    void registerFactory(std::unique_ptr<IFactory<T>> factory) {
        std::string name = factory->getName();
        m_factories[name] = std::move(factory);
        SLOG_DEBUG().message("Registered factory").context("name", name);
    }
    
    /**
     * @brief Create an instance using named factory
     */
    std::shared_ptr<T> create(const std::string& factoryName) {
        auto it = m_factories.find(factoryName);
        if (it != m_factories.end()) {
            return it->second->create();
        }
        
        SLOG_ERROR().message("Factory not found").context("factory_name", factoryName);
        return nullptr;
    }
    
    /**
     * @brief Get all registered factory names
     */
    std::vector<std::string> getFactoryNames() const {
        std::vector<std::string> names;
        for (const auto& pair : m_factories) {
            names.push_back(pair.first);
        }
        return names;
    }
    
private:
    std::unordered_map<std::string, std::unique_ptr<IFactory<T>>> m_factories;
};

/**
 * @brief Builder pattern for complex object construction
 */
template<typename T>
class ServiceBuilder {
public:
    ServiceBuilder() : m_instance(std::make_shared<T>()) {}
    
    /**
     * @brief Configure the service
     */
    template<typename ConfigFunc>
    ServiceBuilder& configure(ConfigFunc func) {
        func(*m_instance);
        return *this;
    }
    
    /**
     * @brief Add a dependency
     */
    template<typename TDep>
    ServiceBuilder& withDependency(std::shared_ptr<TDep> dependency) {
        // This would require setter injection or property injection
        // Implementation depends on service design
        return *this;
    }
    
    /**
     * @brief Build the configured service
     */
    std::shared_ptr<T> build() {
        return m_instance;
    }
    
private:
    std::shared_ptr<T> m_instance;
};

} // namespace burwell

#endif // BURWELL_SERVICE_FACTORY_H