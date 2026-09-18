#pragma once
#include "IModemEngine.h"
#include <unordered_map>
#include <memory>
#include <functional>
#include <string>

class ModemRegistry {
public:
    using Factory = std::function<std::unique_ptr<IModemEngine>()>;
    
    static ModemRegistry& get() {
        static ModemRegistry instance;
        return instance;
    }
    
    void register_modem(const std::string& id, Factory factory) {
        factories_[id] = factory;
    }
    
    std::unique_ptr<IModemEngine> instantiate(const std::string& id) {
        auto it = factories_.find(id);
        if (it != factories_.end()) {
            return it->second();
        }
        return nullptr;
    }
    
    const std::unordered_map<std::string, Factory>& all() const {
        return factories_;
    }

private:
    ModemRegistry() = default;
    std::unordered_map<std::string, Factory> factories_;
};

#define REGISTER_MODEM(CLASS_NAME, ID_STR) \
    static bool _reg_##CLASS_NAME = []() { \
        ModemRegistry::get().register_modem(ID_STR, []() { \
            return std::make_unique<CLASS_NAME>(); \
        }); \
        return true; \
    }();
