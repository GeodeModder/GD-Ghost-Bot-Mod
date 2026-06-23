#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <vector>

using namespace geode::prelude;

/**
 * --- DATA STRUCTURE: GhostFrame ---
 * A clean, singular definition of a frame in our ghost tape.
 */
struct GhostFrame {
    cocos2d::CCPoint position;
    float rotation;
    bool isHolding;
    int iconID;
    int iconType;
};

/**
 * --- THE FORTRESS CLASS ---
 * We consolidate everything into one modify block to ensure 
 * state is shared and accessible without hacks.
 */
class $modify(GhostPlayLayer, PlayLayer) {
    
    /**
     * --- GEODE FIELDS ---
     * This is where we store all our state variables. 
     * This fixes the "improper custom fields" error.
     */
    struct $add(Fields) {
        std::vector<GhostFrame> m_ghostTape;
        size_t m_playbackIndex = 0;
        bool m_isRecording = false;
        bool m_hasDied = false;
        bool m_isHoldingInput = false;
        SimplePlayer* m_ghostPlayer = nullptr;
    };

    // --- A. THE DEATH TRAP ---
    void destroyPlayer(PlayerObject* p0, GameObject* p1) {
        m_fields->m_hasDied = true;
        log::debug("Ghost: Player death detected. Stopping tape.");
        PlayLayer::destroyPlayer(p0, p1);
    }

    // --- B. THE INPUT CAPTURE ---
    void handleButton(bool down, int button, bool isPlayer1) {
        if (isPlayer1) {
            m_fields->m_isHoldingInput = down;
        }
        PlayLayer::handleButton(down, button, isPlayer1);
    }

    // --- C. INITIALIZATION ---
    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;

        // Reset all instance state
        m_fields->m_ghostTape.clear();
        m_fields->m_playbackIndex = 0;
        m_fields->m_hasDied = false;
        m_fields->m_isRecording = !useReplay;
        
        loadFromStorage(level->m_levelID);

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            spawnGhost();
        }

        return true;
    }

    // --- D. GHOST SPAWNER ---
    void spawnGhost() {
        if (m_fields->m_ghostPlayer) return;

        auto gm = GameManager::sharedState();
        m_fields->m_ghostPlayer = SimplePlayer::create(gm->getPlayerFrame());
        
        if (m_fields->m_ghostPlayer) {
            m_fields->m_ghostPlayer->setOpacity(130);
            m_fields->m_ghostPlayer->setColor(cocos2d::ccColor3B{0, 255, 255});
            m_fields->m_ghostPlayer->setVisible(false);
            this->m_objectLayer->addChild(m_fields->m_ghostPlayer, 999);
        }
    }

    // --- E. PERSISTENCE LAYER ---
    void saveToStorage(int levelID) {
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
        log::debug("Ghost: Saved {} frames.", m_fields->m_ghostTape.size());
    }

    void loadFromStorage(int levelID) {
        std::string key = "ghost_tape_" + std::to_string(levelID);
        auto data = Mod::get()->getSavedValue<matjson::Value>(key);
        
        m_fields->m_ghostTape.clear();
        if (data.isArray()) {
            for (auto& item : data.asArray().unwrap()) {
                m_fields->m_ghostTape.push_back({
                    {(float)item["x"].asDouble().unwrap(), (float)item["y"].asDouble().unwrap()},
                    (float)item["rot"].asDouble().unwrap(),
                    item["hold"].asBool().unwrap(),
                    (int)item["id"].asInt().unwrap(),
                    (int)IconType::Cube
                });
            }
            log::debug("Ghost: Loaded {} frames.", m_fields->m_ghostTape.size());
        }
    }

    // --- F. THE HEARTBEAT ---
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!this->m_player1) return;

        // Recording Phase
        if (m_fields->m_isRecording && !m_fields->m_hasDied) {
            m_fields->m_ghostTape.push_back({
                this->m_player1->getPosition(),
                this->m_player1->getRotation(),
                m_fields->m_isHoldingInput,
                GameManager::sharedState()->getPlayerFrame(),
                (int)IconType::Cube
            });
        }
        // Playback Phase
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
                if (m_fields->m_ghostPlayer) m_fields->m_ghostPlayer->setVisible(false);
            }
        }
    }

    // --- G. RESET LOGIC ---
    void resetLevel() {
        PlayLayer::resetLevel();
        
        if (m_fields->m_hasDied) {
            m_fields->m_ghostTape.clear();
        }
        
        m_fields->m_hasDied = false;
        m_fields->m_playbackIndex = 0;
        
        if (m_fields->m_ghostPlayer) {
            m_fields->m_ghostPlayer->setVisible(true);
            m_fields->m_ghostPlayer->updatePlayerFrame(GameManager::sharedState()->getPlayerFrame(), IconType::Cube);
        }
    }

    // --- H. CHECKPOINT HANDLING (Integrated) ---
    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        m_fields->m_hasDied = false; // Reset death flag
    }

    // --- I. EXIT LOGIC ---
    void onExit() {
        if (m_fields->m_isRecording && !m_fields->m_hasDied) {
            saveToStorage(this->m_level->m_levelID);
        }
        PlayLayer::onExit();
    }
};
