#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(GhostBotLayer, PlayLayer) {
    struct Fields {
        PlayerObject* m_ghostBot = nullptr;
    };

    // 2.2081 Correct PlayLayer Hook Signature
    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;

        m_fields->m_ghostBot = nullptr;

        // Safely pull the toggle state out of the 5.7.1 setting cache
        bool isGhostEnabled = Mod::get()->getSettingValue<bool>("ghost-enabled");

        if (isGhostEnabled) {
            spawnGhostBot();
        }
        return true;
    }

    void onExit() {
        PlayLayer::onExit();
        m_fields->m_ghostBot = nullptr;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        
        bool isGhostEnabled = Mod::get()->getSettingValue<bool>("ghost-enabled");
        if (isGhostEnabled) {
            spawnGhostBot();
            if (m_fields->m_ghostBot && this->m_player1) {
                m_fields->m_ghostBot->setPosition(this->m_player1->getPosition());
                m_fields->m_ghostBot->setVisible(true);
            }
        }
    }

    // Dedicated safe spawner with explicit visibility fixes
    void spawnGhostBot() {
        if (!m_fields->m_ghostBot) {
            // Modern 2.2081 factory initialization format
            // 3rd arg: GJBaseGameLayer* (this), 4th arg: cocos2d::CCLayer* (this->m_objectLayer)
            auto ghost = PlayerObject::create(1, 2, this, this->m_objectLayer);
            if (ghost) {
                m_fields->m_ghostBot = ghost;
                
                // CRITICAL VISIBILITY AND RENDER TREE FIXES
                ghost->setScale(this->m_player1->getScale()); 
                ghost->setOpacity(130);          // Makes it look translucent/ghostly 👻
                ghost->setColor({0, 255, 255});  // Distinct cyan hue helper look
                ghost->setVisible(true);

                // Place directly on the main running canvas layer
                this->addChild(ghost, 999); 
            }
        }
    }

    // Safety Engine Hook: Stops menu rendering crashes completely
    void update(float dt) {
        PlayLayer::update(dt);

        // Crash Prevention Guard Rails
        if (this->m_isPaused) return; // Instantly suspends execution thread on pause

        bool isGhostEnabled = Mod::get()->getSettingValue<bool>("ghost-enabled");

        if (isGhostEnabled) {
            if (!m_fields->m_ghostBot) {
                spawnGhostBot();
            }

            if (m_fields->m_ghostBot && this->m_player1) {
                m_fields->m_ghostBot->setVisible(true);
                
                // Track 60 units ahead of your actual position
                float currentX = this->m_player1->getPositionX();
                float currentY = this->m_player1->getPositionY();
                
                m_fields->m_ghostBot->setPosition({currentX + 60.0f, currentY}); 
                m_fields->m_ghostBot->setRotation(this->m_player1->getRotation()); 
                
                // Keep the bot's internal visual animations ticking smoothly
                m_fields->m_ghostBot->update(dt);
            }
        } else {
            if (m_fields->m_ghostBot) {
                m_fields->m_ghostBot->setVisible(false);
            }
        }
    }
};
