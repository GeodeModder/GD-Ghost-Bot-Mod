#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <vector>
#include <string>

using namespace geode::prelude;

/**
 * --- DATA STRUCTURE: GhostFrame ---
 * This is the 'DNA' of our ghost. Each instance of this 
 * represents one frame of gameplay. We store just enough
 * to perfectly reconstruct movement without bloating memory.
 */
struct GhostFrame {
    cocos2d::CCPoint position;
    float rotation;
    bool isHolding;
    int iconID;
    int iconType;
};

/**
 * --- CLASS: GhostPlayLayer ---
 * The fortress. All code is encapsulated here.
 */
class $modify(GhostPlayLayer, PlayLayer) {

    /**
     * --- STATE STORAGE (Fields) ---
     * This is the correct Geode syntax. Geode will automatically
     * inject this struct into the PlayLayer class at runtime.
     * We keep everything here to avoid static variables (which cause memory leaks).
     */
    struct Fields {
        std::vector<GhostFrame> m_ghostTape;
        size_t m_playbackIndex = 0;
        bool m_isRecording = false;
        bool m_hasDied = false;
        bool m_isHoldingInput = false;
        SimplePlayer* m_ghostPlayer = nullptr;
    };

    // --- SECTION 1: CORE HOOKS ---

    // Triggered when the player crashes.
    void destroyPlayer(PlayerObject* p0, GameObject* p1) {
        log::debug("Ghost: Player death detected. Signaling state: DEAD.");
        
        // Mark as dead so we stop recording and preserve the "fail" state
        m_fields->m_hasDied = true;
        
        // Pass to the game so the explosion animation plays
        PlayLayer::destroyPlayer(p0, p1);
    }

    // Triggered on every touch/release event.
    void handleButton(bool down, int button, bool isPlayer1) {
        if (isPlayer1) {
            // Update our internal hold state
            m_fields->m_isHoldingInput = down;
        }
        
        // Let the game handle the actual physics
        PlayLayer::handleButton(down, button, isPlayer1);
    }

    // --- SECTION 2: LIFECYCLE ---

    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        // Run the original initializer first
        if (!PlayLayer::init(level, useReplay, dontCheat)) {
            return false;
        }

        log::debug("Ghost: Initializing level {}", level->m_levelID);

        // Reset all instance state
        m_fields->m_ghostTape.clear();
        m_fields->m_playbackIndex = 0;
        m_fields->m_hasDied = false;
        m_fields->m_isRecording = !useReplay;
        
        // Restore previous data if it exists
        this->loadGhostData(level->m_levelID);

        // Spawn visual ghost if the user enabled the mod
        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            this->createGhostPlayer();
        }

        return true;
    }

    // Called every frame. The heart of the bot.
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!this->m_player1) return;

        // --- RECORDING PHASE ---
        if (m_fields->m_isRecording && !m_fields->m_hasDied) {
            GhostFrame frame;
            frame.position = this->m_player1->getPosition();
            frame.rotation = this->m_player1->getRotation();
            frame.isHolding = m_fields->m_isHoldingInput;
            frame.iconID = GameManager::sharedState()->getPlayerFrame();
            frame.iconType = (int)IconType::Cube;
            
            m_fields->m_ghostTape.push_back(frame);
        }

        // --- PLAYBACK PHASE ---
        else if (!m_fields->m_isRecording && !m_fields->m_ghostTape.empty()) {
            if (m_fields->m_playbackIndex < m_fields->m_ghostTape.size()) {
                auto& frame = m_fields->m_ghostTape[m_fields->m_playbackIndex];
                
                if (m_fields->m_ghostPlayer) {
                    m_fields->m_ghostPlayer->setVisible(true);
                    m_fields->m_ghostPlayer->setPosition(frame.position);
                    m_fields->m_ghostPlayer->setRotation(frame.rotation);
                    m_fields->m_ghostPlayer->setScale(this->m_player1->getScale());
                }
                m_fields->m_playbackIndex++;
            } else {
                // End of playback
                if (m_fields->m_ghostPlayer) m_fields->m_ghostPlayer->setVisible(false);
            }
        }
    }

    // Reset logic for restarts.
    void resetLevel() {
        PlayLayer::resetLevel();
        
        // If we died, we might want to clear the recording to start fresh
        if (m_fields->m_hasDied) {
            log::debug("Ghost: Reset triggered after death. Clearing tape.");
            m_fields->m_ghostTape.clear();
        }
        
        m_fields->m_hasDied = false;
        m_fields->m_playbackIndex = 0;
        
        if (m_fields->m_ghostPlayer) {
            m_fields->m_ghostPlayer->setVisible(true);
            m_fields->m_ghostPlayer->updatePlayerFrame(GameManager::sharedState()->getPlayerFrame(), IconType::Cube);
        }
    }

    // --- SECTION 3: HELPER METHODS ---

    void createGhostPlayer() {
        if (m_fields->m_ghostPlayer) return;

        auto gm = GameManager::sharedState();
        m_fields->m_ghostPlayer = SimplePlayer::create(gm->getPlayerFrame());
        
        if (m_fields->m_ghostPlayer) {
            m_fields->m_ghostPlayer->setOpacity(130);
            m_fields->m_ghostPlayer->setColor(cocos2d::ccColor3B{0, 255, 255});
            m_fields->m_ghostPlayer->setVisible(false);
            this->m_objectLayer->addChild(m_fields->m_ghostPlayer, 999);
            log::debug("Ghost: Visual instance created.");
        }
    }

    void saveGhostData(int levelID) {
        std::vector<matjson::Value> arr;
        for (const auto& frame : m_fields->m_ghostTape) {
            arr.push_back(matjson::makeObject({
                {"x", frame.position.x},
                {"y", frame.position.y},
                {"rot", frame.rotation},
                {"hold", frame.isHolding},
                {"id", frame.iconID}
            }));
        }
        std::string key = "ghost_tape_" + std::to_string(levelID);
        Mod::get()->setSavedValue(key, matjson::Value(arr));
        log::debug("Ghost: Data saved to disk.");
    }

    void loadGhostData(int levelID) {
        std::string key = "ghost_tape_" + std::to_string(levelID);
        auto data = Mod::get()->getSavedValue<matjson::Value>(key);
        
        m_fields->m_ghostTape.clear();
        
        if (data.isArray()) {
            auto arr = data.asArray().unwrap();
            for (auto& item : arr) {
                GhostFrame frame;
                frame.position = {
                    (float)item["x"].asDouble().unwrap(), 
                    (float)item["y"].asDouble().unwrap()
                };
                frame.rotation = (float)item["rot"].asDouble().unwrap();
                frame.isHolding = item["hold"].asBool().unwrap();
                frame.iconID = (int)item["id"].asInt().unwrap();
                frame.iconType = (int)IconType::Cube;
                m_fields->m_ghostTape.push_back(frame);
            }
            log::debug("Ghost: {} frames loaded.", m_fields->m_ghostTape.size());
        }
    }

    // Handle exiting the level (Clean up memory)
    void onExit() {
        if (m_fields->m_isRecording && !m_fields->m_hasDied) {
            this->saveGhostData(this->m_level->m_levelID);
        }
        
        // Remove ghost from parent to prevent memory leaks
        if (m_fields->m_ghostPlayer) {
            m_fields->m_ghostPlayer->removeFromParent();
            m_fields->m_ghostPlayer = nullptr;
        }
        
        PlayLayer::onExit();
    }
};
