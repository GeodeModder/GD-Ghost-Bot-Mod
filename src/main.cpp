#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <vector>
#include <Geode/utils/JsonValidation.hpp>

using namespace geode::prelude;

// --- Global Memory ---
struct GhostFrame {
    cocos2d::CCPoint position;
    float rotation;
    IconType iconType;
    int iconID;
};

static inline std::vector<GhostFrame> g_ghostTape;
static inline size_t g_liveFrameCounter = 0;
static inline SimplePlayer* g_mirrorGhost = nullptr;
static inline IconType g_lastGhostType = IconType::Cube;

// --- Persistence Logic ---
void saveGhostData() {
    auto data = matjson::Array();
    for (const auto& frame : g_ghostTape) {
        auto obj = matjson::Object();
        obj["x"] = frame.position.x;
        obj["y"] = frame.position.y;
        obj["rot"] = frame.rotation;
        obj["type"] = (int)frame.iconType;
        obj["id"] = frame.iconID;
        data.push_back(obj);
    }
    Mod::get()->saveData("ghost_data.json", data);
}

void loadGhostData() {
    auto data = Mod::get()->loadData("ghost_data.json");
    if (data.isArray()) {
        g_ghostTape.clear();
        for (auto& item : data.asArray()) {
            g_ghostTape.push_back({
                { (float)item["x"].asDouble(), (float)item["y"].asDouble() },
                (float)item["rot"].asDouble(),
                (IconType)item["type"].asInt(),
                item["id"].asInt()
            });
        }
    }
}

// ... [Keep your existing getCurrentIconType and getIconIdForType functions here] ...

class $modify(GhostPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;
        
        g_liveFrameCounter = 0;
        g_mirrorGhost = nullptr;
        
        // Load existing ghost on startup!
        loadGhostData();

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            spawnGhostBot(this);
        }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g_liveFrameCounter = 0;
        // ... [Your existing reset logic] ...
    }

    // Call this whenever you finish a practice run to lock it in forever
    void onQuit() {
        saveGhostData();
        PlayLayer::onQuit();
    }

    // ... [Keep your existing postUpdate and checkpoint logic] ...
};