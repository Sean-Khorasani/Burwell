#ifndef BURWELL_DEPENDENCY_INJECTION_H
#define BURWELL_DEPENDENCY_INJECTION_H

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <functional>
#include <string>
#include <mutex>
#include <stdexcept>

namespace burwell {

/**
 * @brief Simple dependency injection container
 * 
 * Provides service registration and resolution with automatic dependency injection.
 * Supports singleton and transient lifetimes.
 */
class DIContainer {
public:
    /**
     * @brief Service lifetime options
     */
    enum class Lifetime {
        SINGLETON,   // Single instance for entire application
        TRANSIENT    // New instance for each request
    };
    
    DIContainer() = default;
    ~DIContainer() = default;
    
    // Disable copy
    DIContainer(const DIContainer&) = delete;
    DIContainer& operator=(const DIContainer&) = delete;
    
    /**
     * @brief Register a service factory
     * @tparam TInterface Interface type
     * @tparam TImplementation Implementation type
     * @param lifetime Service lifetime
     */
    template<typename TInterface, typename TImplementation>
    void registerService(Lifetime lifetime = Lifetime::SINGLETON) {
        static_assert(std::is_base_of<TInterface, TImplementation>::value,
                     "TImplementation must inherit from TInterface");
        
        auto factory = []() -> std::shared_ptr<TInterface> {
            return std::make_shared<TImplementation>();
        };
        
        registerFactory<TInterface>(factory, lifetime);
    }
    
    /**
     * @brief Register a service factory with custom creation logic
     * @tparam TInterface Interface type
     * @param factory Factory function
     * @param lifetime Service lifetime
     */
    template<typename TInterface>
    void registerFactory(std::function<std::shared_ptr<TInterface>()> factory,
                        Lifetime lifetime = Lifetime::SINGLETON) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        ServiceDescriptor descriptor;
        descriptor.factory = [factory]() -> std::shared_ptr<void> {
            return factory();
        };
        descriptor.lifetime = lifetime;
        
        m_services[std::type_index(typeid(TInterface))] = descriptor;
    }
    
    /**
     * @brief Register an existing instance as a singleton
     * @tparam TInterface Interface type
     * @param instance Existing instance
     */
    template<typename TInterface>
    void registerInstance(std::shared_ptr<TInterface> instance) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        ServiceDescriptor descriptor;
        descriptor.instance = instance;
        descriptor.lifetime = Lifetime::SINGLETON;
        
        m_services[std::type_index(typeid(TInterface))] = descriptor;
    }
    
    /**
     * @brief Resolve a service
     * @tparam TInterface Interface type to resolve
     * @return Shared pointer to the service
     * @throws std::runtime_error if service not registered
     */
    template<typename TInterface>
    std::shared_ptr<TInterface> resolve() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto typeIndex = std::type_index(typeid(TInterface));
        auto it = m_services.find(typeIndex);
        
        if (it == m_services.end()) {
            throw std::runtime_error("Service not registered: " + 
                                   std::string(typeid(TInterface).name()));
        }
        
        auto& descriptor = it->second;
        
        // If singleton and already created, return existing instance
        if (descriptor.lifetime == Lifetime::SINGLETON && descriptor.instance) {
            return std::static_pointer_cast<TInterface>(descriptor.instance);
        }
        
        // Create new instance
        if (descriptor.factory) {
            auto instance = descriptor.factory();
            
            // Store singleton instances
            if (descriptor.lifetime == Lifetime::SINGLETON) {
                descriptor.instance = instance;
            }
            
            return std::static_pointer_cast<TInterface>(instance);
        }
        
        throw std::runtime_error("No factory or instance available for service");
    }
    
    /**
     * @brief Try to resolve a service
     * @tparam TInterface Interface type to resolve
     * @return Shared pointer to the service or nullptr if not registered
     */
    template<typename TInterface>
    std::shared_ptr<TInterface> tryResolve() noexcept {
        try {
            return resolve<TInterface>();
        } catch (...) {
            return nullptr;
        }
    }
    
    /**
     * @brief Check if a service is registered
     * @tparam TInterface Interface type to check
     * @return true if registered
     */
    template<typename TInterface>
    bool isRegistered() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_services.find(std::type_index(typeid(TInterface))) != m_services.end();
    }
    
    /**
     * @brief Clear all registrations
     */
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_services.clear();
    }
    
private:
    struct ServiceDescriptor {
        std::function<std::shared_ptr<void>()> factory;
        std::shared_ptr<void> instance;
        Lifetime lifetime;
    };
    
    mutable std::mutex m_mutex;
    std::unordered_map<std::type_index, ServiceDescriptor> m_services;
    
    /**
     * @brief Create a service injector for constructor injection
     * @tparam T Service type
     * @return Service injector that resolves dependencies
     */
    template<typename T>
    class ServiceInjector {
    public:
        explicit ServiceInjector(DIContainer* container) : m_container(container) {}
        
        template<typename TDep>
        operator std::shared_ptr<TDep>() {
            return m_container->resolve<TDep>();
        }
        
    private:
        DIContainer* m_container;
    };
    
    template<typename T>
    ServiceInjector<T> makeServiceInjector() {
        return ServiceInjector<T>(this);
    }
};

/**
 * @brief Service locator pattern for global access
 */
class ServiceLocator {
public:
    /**
     * @brief Get the global DI container
     */
    static DIContainer& getContainer() {
        static DIContainer container;
        return container;
    }
    
    /**
     * @brief Register a service globally
     */
    template<typename TInterface, typename TImplementation>
    static void registerService(DIContainer::Lifetime lifetime = DIContainer::Lifetime::SINGLETON) {
        getContainer().registerService<TInterface, TImplementation>(lifetime);
    }
    
    /**
     * @brief Resolve a service globally
     */
    template<typename TInterface>
    static std::shared_ptr<TInterface> resolve() {
        return getContainer().resolve<TInterface>();
    }
};

/**
 * @brief RAII helper for scoped service registration
 */
template<typename TInterface>
class ScopedService {
public:
    ScopedService(std::shared_ptr<TInterface> instance) {
        ServiceLocator::getContainer().registerInstance<TInterface>(instance);
    }
    
    ~ScopedService() {
        // Note: This doesn't unregister the service, just ensures cleanup
        // In a full implementation, we'd track and remove the registration
    }
};

/**
 * @brief Attribute-based injection (for future use with reflection)
 */
#define INJECT(Type) Type

/**
 * @brief Constructor injection helper macro
 */
#define DI_CONSTRUCTOR(...) \
    explicit ClassName(burwell::DIContainer::ServiceInjector<ClassName> injector) \
        : __VA_ARGS__ {}

} // namespace burwell

#endif // BURWELL_DEPENDENCY_INJECTION_H