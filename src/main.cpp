#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

// Global tracking pointer to ensure ONLY ONE ghost exists and can be frozen
static inline PlayerObject* g_ghostBot = nullptr;

class $modify(GhostPlayerObject, PlayerObject) {
    void update(float dt) {
        // THE ULTIMATE OVERRIDE: If the engine is trying to update our ghost,
        // stop it dead in its tracks. Do not run native physics or movement.
        if (this == g_ghostBot) {
            return; 
        }
        PlayerObject::update(dt);
    }
};

class $modify(GhostBotLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCheat) {
        if (!PlayLayer::init(level, useReplay, dontCheat)) return false;
        g_ghostBot = nullptr;

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            spawnGhostBot();
        }
        return true;
    }

    void onExit() {
        PlayLayer::onExit();
        g_ghostBot = nullptr;
    }

    void resetLevel() {
        // Obliterate the ghost object on death/reset to fix the duplicate bugs
        if (g_ghostBot) {
            g_ghostBot->removeFromParent();
            g_ghostBot = nullptr;
        }

        PlayLayer::resetLevel();

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            spawnGhostBot();
            if (g_ghostBot && this->m_player1) {
                g_ghostBot->setPosition(this->m_player1->getPosition());
            }
        }
    }

    void destroyPlayer(PlayerObject* p1, GameObject* p2) {
        if (g_ghostBot && p1 == g_ghostBot) return; 
        PlayLayer::destroyPlayer(p1, p2);
    }

    void spawnGhostBot() {
        // Safeguard: If a ghost already exists, do NOT spawn another one
        if (!g_ghostBot && this->m_objectLayer) {
            auto ghost = PlayerObject::create(1, 2, nullptr, this->m_objectLayer, false);
            if (ghost) {
                g_ghostBot = ghost;
                
                ghost->unscheduleUpdate();
                
                // Nuclear trail deletion
                if (ghost->m_regularTrail) ghost->m_regularTrail->setVisible(false);
                if (ghost->m_shipStreak) ghost->m_shipStreak->setVisible(false);
                if (ghost->m_waveTrail) ghost->m_waveTrail->setVisible(false);
                
                ghost->setScale(this->m_player1->getScale()); 
                ghost->setOpacity(130);
                ghost->setColor({0, 255, 255}); 
                ghost->setVisible(true);

                this->addChild(ghost, 999); 
                syncGhostGamemode(ghost, this->m_player1);
            }
        }
    }

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

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (this->m_isPaused) return;

        if (Mod::get()->getSettingValue<bool>("ghost-enabled")) {
            if (!g_ghostBot) spawnGhostBot();

            auto ghost = g_ghostBot;
            auto player = this->m_player1;

            if (ghost && player) {
                ghost->setVisible(true);
                syncGhostGamemode(ghost, player);

                if (ghost->m_regularTrail) ghost->m_regularTrail->setVisible(false);
                if (ghost->m_shipStreak) ghost->m_shipStreak->setVisible(false);
                if (ghost->m_waveTrail) ghost->m_waveTrail->setVisible(false);

                // With native physics completely dead, our manual coordinate syncing wins!
                float currentX = player->getPositionX();
                float currentY = player->getPositionY();
                
                // If you want it exactly on top of you, change "+ 60.0f" to "+ 0.0f"
                ghost->setPosition({currentX + 60.0f, currentY}); 
                ghost->setRotation(player->getRotation()); 
            }
        } else {
            if (g_ghostBot) {
                g_ghostBot->setVisible(false);
            }
        }
    }
};
