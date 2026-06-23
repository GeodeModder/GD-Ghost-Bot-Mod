#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(GhostBotLayer, PlayLayer) {
    // Geode 5.7.1 isolated safety struct memory wrapper
    struct Fields {
        PlayerObject* m_ghostBot = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;

        m_fields->m_ghostBot = nullptr;

        // Config Cache Access syntax from your dictionary
        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            spawnGhostBot();
        }
        return true;
    }

    void onExit() {
        PlayLayer::onExit();
        m_fields->m_ghostBot = nullptr;
    }

    void resetLevel() {
        // CRITICAL BUGFIX: Vaporize the old entity completely on reset 
        // This stops the engine from creating duplicate zombie clones on the screen!
        if (m_fields->m_ghostBot) {
            m_fields->m_ghostBot->removeFromParent();
            m_fields->m_ghostBot = nullptr;
        }

        PlayLayer::resetLevel();

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            spawnGhostBot();
            if (m_fields->m_ghostBot && this->m_player1) {
                m_fields->m_ghostBot->setPosition(this->m_player1->getPosition());
                m_fields->m_ghostBot->setVisible(true);
            }
        }
    }

    // Immortal Hook: Ensure the ghost bypasses standard hazard destruction routines
    void destroyPlayer(PlayerObject* p1, GameObject* p2) {
        if (m_fields->m_ghostBot && p1 == m_fields->m_ghostBot) return; 
        PlayLayer::destroyPlayer(p1, p2);
    }

    void spawnGhostBot() {
        if (!m_fields->m_ghostBot && this->m_objectLayer) {
            // FIX: Using the exact 4-argument factory layout parameter from your spec sheet
            auto ghost = PlayerObject::create(1, 2, this, this->m_objectLayer);
            if (ghost) {
                m_fields->m_ghostBot = ghost;
                
                // Visual setup
                ghost->setScale(this->m_player1->getScale()); 
                ghost->setOpacity(130);
                ghost->setColor({0, 255, 255}); // Distinct cyan visual outline
                ghost->setVisible(true);

                // Add directly to the active canvas root parent layer container
                this->addChild(ghost, 999); 

                // FIX: Force immediate state sync right at birth. 
                // This ensures it transforms into a Ship/Wave/Ball instantly if a level starts inside a portal!
                syncGhostGamemode(ghost, this->m_player1);
            }
        }
    }

    // Robust gamemode state mapping helper
    void syncGhostGamemode(PlayerObject* ghost, PlayerObject* player) {
        if (!ghost || !player) return;
        
        if (ghost->m_isShip != player->m_isShip) ghost->toggleFlyMode(player->m_isShip, true);
        if (ghost->m_isBall != player->m_isBall) ghost->toggleRollMode(player->m_isBall, true);
        if (ghost->m_isBird != player->m_isBird) ghost->toggleBirdMode(player->m_isBird, true);
        if (ghost->m_isDart != player->m_isDart) ghost->toggleDartMode(player->m_isDart, true);
        if (ghost->m_isRobot != player->m_isRobot) ghost->toggleRobotMode(player->m_isRobot, true);
        if (ghost->m_isSpider != player->m_isSpider) ghost->toggleSpiderMode(player->m_isSpider, true);
        if (ghost->m_isSwing != player->m_isSwing) ghost->toggleSwingMode(player->m_isSwing, true);
        
        ghost->setScale(player->getScale());
    }

    // FIX: Using postUpdate to force the spatial override vectors AFTER the physics loop finishes computing
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        // Safety evaluation statement to prevent thread rendering crashes during pause states
        if (this->m_isPaused) return;

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            if (!m_fields->m_ghostBot) spawnGhostBot();

            auto ghost = m_fields->m_ghostBot;
            auto player = this->m_player1;

            if (ghost && player) {
                ghost->setVisible(true);
                
                // Keep gamemode modifications matched frame-by-frame
                syncGhostGamemode(ghost, player);

                // Let internal animation vectors tick smoothly
                ghost->update(dt);

                // Absolute translation execution pass to lock the scout exactly 60 units in front
                float currentX = player->getPositionX();
                float currentY = player->getPositionY();
                
                ghost->setPosition({currentX + 60.0f, currentY}); 
                ghost->setRotation(player->getRotation()); 
            }
        } else {
            if (m_fields->m_ghostBot) {
                m_fields->m_ghostBot->setVisible(false);
            }
        }
    }
};
