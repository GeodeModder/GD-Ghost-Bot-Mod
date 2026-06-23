#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <vector>

using namespace geode::prelude;

// --- 1. DATA STRUCTURES & GLOBALS ---
struct GhostFrame {
    cocos2d::CCPoint position;
    float rotation;
    IconType iconType;
    int iconID;
};

static inline std::vector<GhostFrame> g_ghostTape;
static inline std::vector<size_t> g_checkpointTapeMarks;
static inline size_t g_liveFrameCounter = 0;
static inline SimplePlayer* g_mirrorGhost = nullptr;
static inline GJGameLevel* g_recordedLevel = nullptr;
static inline IconType g_lastGhostType = IconType::Cube;

constexpr size_t OFFSET_FRAMES = 80;

// --- 2. HELPERS (Persistence & Logic) ---
void saveGhostData() {
    matjson::Value data = matjson::Value(std::vector<matjson::Value>{});
    for (const auto& frame : g_ghostTape) {
        matjson::Value obj = matjson::Value();
        obj["x"] = frame.position.x;
        obj["y"] = frame.position.y;
        obj["rot"] = frame.rotation;
        obj["type"] = (int)frame.iconType;
        obj["id"] = frame.iconID;
        data.push_back(obj);
    }
    Mod::get()->setSavedValue<matjson::Value>("ghost_tape", data);
}

void loadGhostData() {
    auto data = Mod::get()->getSavedValue<matjson::Value>("ghost_tape");
    if (data.isArray()) {
        g_ghostTape.clear();
        for (auto& item : data.asArray().unwrap()) {
            g_ghostTape.push_back({
                {(float)item["x"].asDouble().unwrap(), (float)item["y"].asDouble().unwrap()},
                (float)item["rot"].asDouble().unwrap(),
                (IconType)item["type"].asInt().unwrap(),
                static_cast<int>(item["id"].asInt().unwrap())
            });
        }
    }
}

IconType getCurrentIconType(PlayerObject* player) {
    if (player->m_isShip) return IconType::Ship;
    if (player->m_isBall) return IconType::Ball;
    if (player->m_isBird) return IconType::Ufo;
    if (player->m_isDart) return IconType::Wave;
    if (player->m_isRobot) return IconType::Robot;
    if (player->m_isSpider) return IconType::Spider;
    if (player->m_isSwing) return IconType::Swing;
    return IconType::Cube;
}

int getIconIdForType(IconType type) {
    auto gm = GameManager::sharedState();
    if (!gm) return 1;
    switch (type) {
        case IconType::Ship:   return gm->getPlayerShip();
        case IconType::Ball:   return gm->getPlayerBall();
        case IconType::Ufo:    return gm->getPlayerBird();
        case IconType::Wave:   return gm->getPlayerDart();
        case IconType::Robot:  return gm->getPlayerRobot();
        case IconType::Spider: return gm->getPlayerSpider();
        case IconType::Swing:  return gm->getPlayerSwing();
        default:               return gm->getPlayerFrame();
    }
}

void spawnGhostBot(PlayLayer* playLayer) {
    if (g_mirrorGhost || !playLayer->m_objectLayer) return;
    auto gm = GameManager::sharedState();
    int defaultCubeID = gm ? gm->getPlayerFrame() : 1;
    auto ghost = SimplePlayer::create(defaultCubeID);
    if (ghost) {
        ghost->setOpacity(130);
        ghost->setColor(cocos2d::ccColor3B{0, 255, 255});
        playLayer->m_objectLayer->addChild(ghost, 999);
        g_mirrorGhost = ghost;
        g_lastGhostType = IconType::Cube;
    }
}

// --- 3. THE HOOKS ---
class $modify(GhostPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;
        
        g_liveFrameCounter = 0;
        g_mirrorGhost = nullptr;
        
        if (g_recordedLevel != level) {
            g_ghostTape.clear();
            g_checkpointTapeMarks.clear();
            g_recordedLevel = level;
            loadGhostData(); // Auto-load existing ghost
        }

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            spawnGhostBot(this);
        }
        return true;
    }

    void onQuit() {
        saveGhostData(); // Auto-save on exit
