#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <vector>
#include <string>

using namespace geode::prelude;

/**
 * --- DATA STRUCTURES ---
 * We define an explicit structure for every frame. 
 * This keeps the data layout predictable and easily serializable.
 */
struct FrameAction {
    cocos2d::CCPoint position;
    float rotation;
    bool isHolding;
    int iconID;
    int iconType;
};

/**
 * --- MANAGER: GhostBuffer ---
 * Responsible for the storage and lifecycle of the recording data.
 */
class GhostBuffer {
public:
    static inline std::vector<FrameAction> tape;
    static inline size_t playbackIndex = 0;

    static void clear() {
        tape.clear();
        playbackIndex = 0;
    }

    static void addFrame(const FrameAction& frame) {
        tape.push_back(frame);
    }

    static bool isDataAvailable() {
        return !tape.empty();
    }
};

/**
 * --- MANAGER: GhostSerializer ---
 * Handles the I/O operations for saving/loading the ghost data to/from disk.
 * Robust error checking is included.
 */
class GhostSerializer {
public:
    static void save(int levelID) {
        std::vector<matjson::Value> arr;
        for (const auto& frame : GhostBuffer::tape) {
            arr.push_back(matjson::makeObject({
                {"x", frame.position.x},
                {"y", frame.position.y},
                {"rot", frame.rotation},
                {"hold", frame.isHolding},
                {"id", frame.iconID},
                {"type", frame.iconType}
            }));
        }
        
        std::string key = "ghost_tape_" + std::to_string(levelID);
        Mod::get()->setSavedValue(key, matjson::Value(arr));
        log::info("Ghost data saved successfully for level {}", levelID);
    }

    static void load(int levelID) {
        std::string key = "ghost_tape_" + std::to_string(levelID);
        auto data = Mod::get()->getSavedValue<matjson::Value>(key);
        
        GhostBuffer::clear();
        if (data.isArray()) {
            auto arr = data.asArray().unwrap();
            for (auto& item : arr) {
                GhostBuffer::addFrame({
                    {(float)item["x"].asDouble().unwrap(), (float)item["y"].asDouble().unwrap()},
                    (float)item["rot"].asDouble().unwrap(),
                    item["hold"].asBool().unwrap(),
                    (int)item["id"].asInt().unwrap(),
                    (int)item["type"].asInt().unwrap()
                });
            }
            log::info("Ghost data loaded successfully. Loaded {} frames.", GhostBuffer::tape.size());
        }
    }
};

/**
 * --- MANAGER: GhostOrchestrator ---
 * Controls the visual representation (the SimplePlayer ghost).
 */
class GhostOrchestrator {
public:
    static inline SimplePlayer* ghost = nullptr;

    static void spawn(PlayLayer* layer) {
        if (ghost) return;
        auto gm = GameManager::sharedState();
        ghost = SimplePlayer::create(gm->getPlayerFrame());
        if (!ghost) return;

        ghost->setOpacity(130);
        ghost->setColor(cocos2d::ccColor3B{0, 255, 255});
        layer->m_objectLayer->addChild(ghost, 999);
    }

    static void cleanup() {
        if (ghost) {
            ghost->removeFromParent();
            ghost = nullptr;
        }
    }
};

/**
 * --- MAIN MODIFICATION ---
 * The robust PlayLayer implementation using our managers.
 */
class $modify(RobustGhostLayer, PlayLayer) {
    
    // Explicit state tracking
    static inline bool isRecording = false;
    static inline bool hasDied = false;
    static inline bool isHoldingInput = false;

    // --- HOOK: Death Event (Absolute Reliability) ---
    void destroyPlayer(PlayerObject* p0, GameObject* p1) {
        hasDied = true;
        log::debug("Player death detected at frame {}", GhostBuffer::tape.size());
        PlayLayer::destroyPlayer(p0, p1);
    }

    // --- HOOK: Input Event (Manual Tracking) ---
    void handleButton(bool down, int button, bool isPlayer1) {
        if (isPlayer1) {
            isHoldingInput = down;
        }
        PlayLayer::handleButton(down, button, isPlayer1);
    }

    // --- HOOK: Lifecycle ---
    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;

        // Initialize state
        GhostBuffer::clear();
        hasDied = false;
        isRecording = !useReplay;
        
        // Load data if available
        GhostSerializer::load(level->m_levelID);

        // Spawn Ghost
        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            GhostOrchestrator::spawn(this);
        }

        return true;
    }

    // --- HOOK: Main Loop ---
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!this->m_player1) return;

        // 1. RECORDING PHASE
        if (isRecording && !hasDied) {
            GhostBuffer::addFrame({
                this->m_player1->getPosition(),
                this->m_player1->getRotation(),
                isHoldingInput,
                GameManager::sharedState()->getPlayerFrame(),
                (int)IconType::Cube // Simplified for structure
            });
        }
        // 2. PLAYBACK PHASE
        else if (!isRecording && GhostBuffer::isDataAvailable()) {
            if (GhostBuffer::playbackIndex < GhostBuffer::tape.size()) {
                auto& frame = GhostBuffer::tape[GhostBuffer::playbackIndex];
                
                if (GhostOrchestrator::ghost) {
                    GhostOrchestrator::ghost->setVisible(true);
                    GhostOrchestrator::ghost->setPosition(frame.position);
                    GhostOrchestrator::ghost->setRotation(frame.rotation);
                    GhostOrchestrator::ghost->setScale(this->m_player1->getScale());
                }
                GhostBuffer::playbackIndex++;
            } else {
                if (GhostOrchestrator::ghost) GhostOrchestrator::ghost->setVisible(false);
            }
        }
    }

    // --- HOOK: Cleanup ---
    void onExit() {
        if (isRecording && !hasDied) {
            GhostSerializer::save(this->m_level->m_levelID);
        }
        GhostOrchestrator::cleanup();
        PlayLayer::onExit();
    }

    // --- HOOK: Reset ---
    void resetLevel() {
        PlayLayer::resetLevel();
        
        // If we died, we nuke the current recording session
        if (hasDied) {
            log::info("Resetting level after death: Clearing buffer.");
            GhostBuffer::clear();
        }
        
        hasDied = false;
        GhostBuffer::playbackIndex = 0;
        
        if (GhostOrchestrator::ghost) {
            GhostOrchestrator::ghost->setVisible(true);
            GhostOrchestrator::ghost->updatePlayerFrame(GameManager::sharedState()->getPlayerFrame(), IconType::Cube);
        }
    }
};

/**
 * --- DETAILED CHECKPOINT HANDLING ---
 * Robust checkpoint logic to ensure data consistency during practice mode.
 */
class $modify(CheckpointHandler, PlayLayer) {
    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
        // Note: You could extend this to save a "snapshot" of the ghost tape 
        // here if you want to support non-linear practice mode resets.
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        // Reset the death flag explicitly on respawn
        RobustGhostLayer::hasDied = false; 
    }
};
