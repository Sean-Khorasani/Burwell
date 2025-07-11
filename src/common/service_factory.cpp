#include "service_factory.h"
#include "config_manager.h"
#include "structured_logger.h"
#include "thread_pool.h"
#include "../command_parser/command_parser.h"
#include "../llm_connector/llm_connector.h"
#include "../task_engine/task_engine.h"
#include "../ocal/ocal.h"
#include "../environmental_perception/environmental_perception.h"
#include "../ui_module/ui_module.h"
#include "../orchestrator/state_manager.h"
#include "../orchestrator/state_manager_thread_safe.h"
#include "../orchestrator/istate_manager.h"
#include "../orchestrator/execution_engine.h"
#include "../orchestrator/event_manager.h"
#include "../orchestrator/script_manager.h"
#include "../orchestrator/feedback_controller.h"
#include "../orchestrator/conversation_manager.h"
#include <fstream>

namespace burwell {

ServiceFactory::ServiceFactory() {
    SLOG_INFO().message("Initializing service factory")
        .context("component", "ServiceFactory");
}

ServiceFactory::~ServiceFactory() {
    SLOG_INFO().message("Shutting down service factory")
        .context("component", "ServiceFactory");
}

void ServiceFactory::initializeServices(const std::string& configPath) {
    SLOG_INFO().message("Loading configuration")
        .context("component", "ServiceFactory")
        .context("config_path", configPath);
    loadConfiguration(configPath);
    
    // Register core services with dependency injection
    
    // Thread pool (singleton)
    m_container.registerFactory<ThreadPool>([this]() {
        return std::make_shared<ThreadPool>(m_config.threadPoolSize);
    }, DIContainer::Lifetime::SINGLETON);
    
    // State manager (singleton, thread-safe version)
    m_container.registerFactory<StateManager>([this]() {
        return createStateManager(m_config.useThreadSafeStateManager);
    }, DIContainer::Lifetime::SINGLETON);
    
    // Event manager (singleton)
    m_container.registerFactory<EventManager>([this]() {
        return createEventManager();
    }, DIContainer::Lifetime::SINGLETON);
    
    // Other core services
    m_container.registerFactory<CommandParser>([this]() {
        return createCommandParser();
    }, DIContainer::Lifetime::SINGLETON);
    
    m_container.registerFactory<TaskEngine>([this]() {
        return createTaskEngine();
    }, DIContainer::Lifetime::SINGLETON);
    
    m_container.registerFactory<OCAL>([this]() {
        return createOCAL();
    }, DIContainer::Lifetime::SINGLETON);
    
    m_container.registerFactory<EnvironmentalPerception>([this]() {
        return createEnvironmentalPerception();
    }, DIContainer::Lifetime::SINGLETON);
    
    // Transient services
    m_container.registerFactory<ExecutionEngine>([this]() {
        return createExecutionEngine();
    }, DIContainer::Lifetime::TRANSIENT);
    
    m_container.registerFactory<ScriptManager>([this]() {
        return createScriptManager();
    }, DIContainer::Lifetime::TRANSIENT);
    
    SLOG_INFO().message("All core services registered")
        .context("component", "ServiceFactory");
}

std::shared_ptr<CommandParser> ServiceFactory::createCommandParser() {
    SLOG_DEBUG().message("Creating CommandParser")
        .context("component", "ServiceFactory");
    
    auto parser = std::make_shared<CommandParser>();
    
    // Configure command parser
    // parser->loadCommands("config/commands.json");
    
    return parser;
}

std::shared_ptr<LLMConnector> ServiceFactory::createLLMConnector(const std::string& providerName) {
    SLOG_DEBUG().message("Creating LLMConnector")
        .context("component", "ServiceFactory")
        .context("provider", providerName);
    
    // Create appropriate LLM connector based on provider
    auto connector = std::make_shared<LLMConnector>();
    
    // Configure with provider settings
    // connector->configure(m_config.llmProvider);
    
    return connector;
}

std::shared_ptr<TaskEngine> ServiceFactory::createTaskEngine() {
    SLOG_DEBUG().message("Creating TaskEngine")
        .context("component", "ServiceFactory");
    
    auto engine = std::make_shared<TaskEngine>();
    
    // Load task library
    // engine->loadTaskLibrary("config/tasks.json");
    
    return engine;
}

std::shared_ptr<OCAL> ServiceFactory::createOCAL() {
    SLOG_DEBUG().message("Creating OCAL")
        .context("component", "ServiceFactory");
    
    auto ocal = std::make_shared<OCAL>();
    
    // Initialize OS-specific functionality
    // ocal->initialize();
    
    return ocal;
}

std::shared_ptr<EnvironmentalPerception> ServiceFactory::createEnvironmentalPerception() {
    SLOG_DEBUG().message("Creating EnvironmentalPerception")
        .context("component", "ServiceFactory");
    
    auto perception = std::make_shared<EnvironmentalPerception>();
    
    // Initialize perception modules
    // perception->initialize();
    
    return perception;
}

std::shared_ptr<UIModule> ServiceFactory::createUIModule(const std::string& uiType) {
    SLOG_DEBUG().message("Creating UIModule")
        .context("component", "ServiceFactory")
        .context("ui_type", uiType);
    
    // Create appropriate UI module based on type
    if (uiType == "console") {
        auto ui = std::make_shared<UIModule>();
        // ui->initialize();
        return ui;
    }
    
    // Future: Support GUI, web UI, etc.
    SLOG_WARNING().message("Unknown UI type, falling back to console")
        .context("component", "ServiceFactory")
        .context("ui_type", uiType);
    return std::make_shared<UIModule>();
}

std::shared_ptr<StateManager> ServiceFactory::createStateManager(bool threadSafe) {
    if (threadSafe) {
        SLOG_DEBUG().message("Creating thread-safe StateManager")
            .context("component", "ServiceFactory");
        // Create thread-safe state manager from orchestrator namespace
        auto stateManager = std::make_shared<burwell::StateManager>();
        
        // Configure state manager
        stateManager->setMaxCompletedExecutions(1000);
        stateManager->setMaxActivityLogSize(5000);
        
        return stateManager;
    } else {
        SLOG_DEBUG().message("Creating standard StateManager")
            .context("component", "ServiceFactory");
        auto stateManager = std::make_shared<StateManager>();
        
        // Configure state manager
        stateManager->setMaxCompletedExecutions(1000);
        stateManager->setMaxActivityLogSize(5000);
        
        return stateManager;
    }
}

std::shared_ptr<ExecutionEngine> ServiceFactory::createExecutionEngine() {
    SLOG_DEBUG().message("Creating ExecutionEngine")
        .context("component", "ServiceFactory");
    
    // Create with default constructor
    auto engine = std::make_shared<ExecutionEngine>();
    
    // Dependencies would be injected via setters or configure method
    // auto commandParser = m_container.resolve<CommandParser>();
    // auto taskEngine = m_container.resolve<TaskEngine>();
    // auto ocal = m_container.resolve<OCAL>();
    // auto perception = m_container.resolve<EnvironmentalPerception>();
    // engine->setDependencies(commandParser, taskEngine, ocal, perception);
    
    return engine;
}

std::shared_ptr<EventManager> ServiceFactory::createEventManager() {
    SLOG_DEBUG().message("Creating EventManager")
        .context("component", "ServiceFactory");
    
    // Inject thread pool for async event processing
    auto threadPool = m_container.tryResolve<ThreadPool>();
    
    auto eventManager = std::make_shared<EventManager>();
    
    if (threadPool) {
        // eventManager->setThreadPool(threadPool);
    }
    
    return eventManager;
}

std::shared_ptr<ScriptManager> ServiceFactory::createScriptManager() {
    SLOG_DEBUG().message("Creating ScriptManager")
        .context("component", "ServiceFactory");
    
    auto scriptManager = std::make_shared<ScriptManager>();
    
    // Configure script paths
    // scriptManager->addScriptPath("scripts/");
    // scriptManager->addScriptPath("test_scripts/");
    
    return scriptManager;
}

std::shared_ptr<FeedbackController> ServiceFactory::createFeedbackController() {
    SLOG_DEBUG().message("Creating FeedbackController")
        .context("component", "ServiceFactory");
    
    // Create with default constructor
    auto controller = std::make_shared<FeedbackController>();
    
    // Dependencies would be injected via setters
    // auto perception = m_container.resolve<EnvironmentalPerception>();
    // controller->setEnvironmentalPerception(perception);
    
    return controller;
}

std::shared_ptr<ConversationManager> ServiceFactory::createConversationManager() {
    SLOG_DEBUG().message("Creating ConversationManager")
        .context("component", "ServiceFactory");
    
    // Inject LLM connector
    auto llmConnector = m_container.tryResolve<LLMConnector>();
    
    auto manager = std::make_shared<ConversationManager>();
    
    if (llmConnector) {
        // manager->setLLMConnector(llmConnector);
    }
    
    return manager;
}

void ServiceFactory::loadConfiguration(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            SLOG_WARNING().message("Configuration file not found")
                .context("component", "ServiceFactory")
                .context("config_path", configPath);
            return;
        }
        
        nlohmann::json config;
        file >> config;
        
        // Load service configuration
        if (config.contains("services")) {
            auto& services = config["services"];
            
            m_config.useThreadSafeStateManager = services.value("useThreadSafeStateManager", true);
            m_config.llmProvider = services.value("llmProvider", "default");
            m_config.uiType = services.value("uiType", "console");
            m_config.threadPoolSize = services.value("threadPoolSize", 0);
        }
        
        SLOG_INFO().message("Configuration loaded successfully")
            .context("component", "ServiceFactory");
        
    } catch (const std::exception& e) {
        SLOG_ERROR().message("Failed to load configuration")
            .context("component", "ServiceFactory")
            .context("error", e.what());
    }
}

} // namespace burwell