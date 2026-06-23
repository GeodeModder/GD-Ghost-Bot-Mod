#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <vector>

using namespace geode::prelude;

struct GhostFrame {
    cocos2d::CCPoint position;
    float rotation;
    bool isHolding;
    int iconID;
    int iconType;
};

class $modify(GhostPlayLayer, PlayLayer) {

    struct Fields {
        std::vector<GhostFrame> m_ghostTape;
        size_t m_playbackIndex = 0;
        bool m_isRecording = false;
        bool m_hasDied = false;
        bool m_isHoldingInput = false;
        SimplePlayer* m_ghostPlayer = nullptr;
    };

    void destroyPlayer(PlayerObject* p0, GameObject* p1) {
        m_fields->m_hasDied = true;
        PlayLayer::destroyPlayer(p0, p1);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (isPlayer1) {
            m_fields->m_isHoldingInput = down;
        }
        PlayLayer::handleButton(down, button, isPlayer1);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;

        m_fields->m_ghostTape.clear();
        m_fields->m_playbackIndex = 0;
        m_fields->m_hasDied = false;
        m_fields->m_isRecording = !useReplay;
        
        this->loadGhostData(level->m_levelID);

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            this->createGhostPlayer();
        }

        return true;
    }

    void createGhostPlayer() {
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

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1) return;

        if (m_fields->m_isRecording && !m_fields->m_hasDied) {
            GhostFrame frame;
            frame.position = this->m_player1->getPosition();
            frame.rotation = this->m_player1->getRotation();
            frame.isHolding = m_fields->m_isHoldingInput;
            frame.iconID = GameManager::sharedState()->getPlayerFrame();
            frame.iconType = (int)IconType::Cube;
            m_fields->m_ghostTape.push_back(frame);
        }
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

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->m_hasDied) m_fields->m_ghostTape.clear();
        m_fields->m_hasDied = false;
        m_fields->m_playbackIndex = 0;
        if (m_fields->m_ghostPlayer) {
            m_fields->m_ghostPlayer->setVisible(true);
            m_fields->m_ghostPlayer->updatePlayerFrame(GameManager::sharedState()->getPlayerFrame(), IconType::Cube);
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
        Mod::get()->setSavedValue("ghost_tape_" + std::to_string(levelID), matjson::Value(arr));
    }

    void loadGhostData(int levelID) {
        auto data = Mod::get()->getSavedValue<matjson::Value>("ghost_tape_" + std::to_string(levelID));
        m_fields->m_ghostTape.clear();
        if (data.isArray()) {
            for (auto& item : data.asArray().unwrap()) {
                GhostFrame frame;
                // FIX: Using explicit CCPoint constructor to avoid ambiguity
                frame.position = cocos2d::CCPoint(
                    static_cast<float>(item["x"].asDouble().unwrap()), 
                    static_cast<float>(item["y"].asDouble().unwrap())
                );
                frame.rotation = (float)item["rot"].asDouble().unwrap();
                frame.isHolding = item["hold"].asBool().unwrap();
                frame.iconID = (int)item["id"].asInt().unwrap();
                frame.iconType = (int)IconType::Cube;
                m_fields->m_ghostTape.push_back(frame);
            }
        }
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        m_fields->m_hasDied = false;
    }

    void onExit() {
        if (m_fields->m_isRecording && !m_fields->m_hasDied) {
            this->saveGhostData(this->m_level->m_levelID);
        }
        PlayLayer::onExit();
    }
};
