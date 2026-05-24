#pragma once

#include <cstdlib>
#include <string_view>

inline bool cyclesim_gvm_enabled() {
    const char* env = std::getenv("ENABLE_CYCLESIM_GVM");
    if (env == nullptr) {
        return false;
    }
    const std::string_view value(env);
    return value == "1" || value == "true" || value == "TRUE" || value == "on"
        || value == "ON";
}
