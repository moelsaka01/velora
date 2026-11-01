// Module lifecycle contract for velora-shell submodules. Owner: A.
#pragma once

#include <string_view>

namespace velora {

enum class ModuleResult { Ok, Degraded, Unsupported, Failed };

// A module that returns Degraded/Unsupported/Failed from Start() is logged and
// disabled alone; it must never take the shell down.
class IModule {
public:
    virtual ~IModule() = default;
    virtual std::string_view Name() const = 0;
    virtual ModuleResult Start() = 0;
    virtual void Stop() = 0;  // idempotent; must restore anything it changed
};

}  // namespace velora
