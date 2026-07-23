#pragma once
#include <Geode/Geode.hpp>
#include <string>

using namespace geode::prelude;

// =====================================================================
// DEBUG HELPER
// Fire a popup anywhere we want in-game visibility. This is what
// catches silent failures (like "0 frames recorded") immediately
// instead of discovering them after a full playthrough.
// Flip DEBUG_MODE to false once things are verified stable.
// =====================================================================
namespace GhostDebug {
    constexpr bool DEBUG_MODE = true; // back on while we hunt the save/cancel hang

    inline void popup(const std::string& title, const std::string& message) {
        if (!DEBUG_MODE) return;
        FLAlertLayer::create(title.c_str(), message.c_str(), "OK")->show();
    }

    inline void frameCount(const std::string& context, size_t count) {
        popup("Ghost Debug", context + ": " + std::to_string(count) + " frames");
    }
}
