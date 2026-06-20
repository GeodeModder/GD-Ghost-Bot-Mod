#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Global control variables
bool g_showGhost = false;
static PlayerObject* g_ghostPlayer = nullptr; // A persistent pointer to our specter
float g_timeOffset = 0.5f; // Keep the ghost 0.5 seconds ahead of time

// ==========================================
// 1. THE UI CONTROL (Still the same good stuff)
// ==========================================
class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({60.0f, winSize.height - 35.0f});
        menu->setID("ghost-guide-menu"_spr);
        auto label = CCLabelBMFont::create(g_showGhost ? "Ghost: ON" : "Ghost: OFF", "bigFont.fnt");
        label->setScale(0.4f);
        auto toggleBtn = CCMenuItemSpriteExtra::create(label, this, menu_selector(MyPauseLayer::onToggleGhost));
        toggleBtn->setID("ghost-guide-toggle"_spr);
        menu->addChild(toggleBtn);
        this->addChild(menu, 1000); 
    }
    void onToggleGhost(CCObject* sender) {
        g_showGhost = !g_showGhost;
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto label = static_cast<CCLabelBMFont*>(btn->getNormalImage());
        if (g_showGhost) {
            label->setString("Ghost: ON");
            FLAlertLayer::create("Ghost Bot", "Ghost Guide <g>Enabled</g>!", "OK")->show();
        } else {
            label->setString("Ghost: OFF");
            FLAlertLayer::create("Ghost Bot", "Ghost Guide <r>Disabled</r>!", "OK")->show();
        }
    }
};

// ==========================================
// 2. THE ADVANCED GHOST LOGIC (The cool stuff)
// ==========================================
class $modify(MyPlayLayer, PlayLayer) {
    
    // Feature: Clean up properly when exiting the level
    virtual void onExit() {
        PlayLayer::onExit();
        g_ghostPlayer = nullptr; // Clean up the reference to avoid crashes
    }

    // Feature: Spawning & Transparency
    bool init(GJGameLevel* level, bool useReplay, bool dontRunActions) {
        if (!PlayLayer::init(level, useReplay, dontRunActions)) return false;
        
        g_ghostPlayer = nullptr; // Reset the pointer before creating a new level

        if (g_showGhost) {
            // Spawn a dummy player on the player's layer (layer 2)
            g_ghostPlayer = PlayerObject::create(1, 1, this, this->m_player1->getParent(), false);
            if (g_ghostPlayer) {
                g_ghostPlayer->setOpacity(128); // 50% Translucent
                this->m_player1->getParent()->addChild(g_ghostPlayer, 999);
            }
        }
        return true;
    }

    // Feature: Level Restart/Respawn Sync
    virtual void resetLevel() {
        PlayLayer::resetLevel(); // Run normal respawn logic for the player
        
        // If the ghost exists, force it back to the start too!
        if (g_showGhost && g_ghostPlayer) {
            g_ghostPlayer->setPosition(this->m_player1->getPosition());
        }
    }

    // Feature: Predictive Positioning & Botting Framework
    virtual void update(float dt) {
        PlayLayer::update(dt); // Crucial: allow the game's actual frame-rate handling

        if (g_showGhost && g_ghostPlayer && !this->m_isDead) {
            
            // For now, simple parallel motion: 
            // Keep the ghost X position equal to player's X + speed * time
            // (We will replace this with macro data once we build the reader!)
            float currentX = this->m_player1->getPositionX();
            float currentY = this->m_player1->getPositionY();
            
            g_ghostPlayer->setPosition({currentX + 100.0f, currentY}); // Simple visual offset
            
            // Force the ghost to not rotate naturally, looks cleaner
            g_ghostPlayer->setRotation(0); 

            // (Botting Logic Hook goes here in Phase 4!)
        }
    }

    // Feature: Symbiotic Death Link
    virtual void destroyPlayer(PlayerObject* player, GameObject* obstacle) {
        // If the player dies, and the toggle is ON, and the ghost exists...
        if (g_showGhost && g_ghostPlayer && player == this->m_player1) {
            // Trigger death sequence on the ghost immediately!
            PlayLayer::destroyPlayer(g_ghostPlayer, obstacle);
        }

        // Run the original death logic for the player/ghost
        PlayLayer::destroyPlayer(player, obstacle);
    }
};
