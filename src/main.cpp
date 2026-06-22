#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// FORCE TRUE FOR TESTING: This ensures the ghost attempts to spawn automatically!
bool g_showGhost = true;

// ==========================================
// 1. THE UI CONTROL (Android-Safe Empty Signature)
// ==========================================
class $modify(MyPauseLayer, PauseLayer) {
    bool init() {
        if (!PauseLayer::init()) return false;

        auto menu = this->getChildByID("left-button-menu");
        if (!menu) menu = this->getChildByID("center-button-menu");
        if (!menu) {
            menu = CCMenu::create();
            menu->setPosition({0, 0});
            this->addChild(menu, 1000);
        }

        auto label = CCLabelBMFont::create(g_showGhost ? "Ghost: ON" : "Ghost: OFF", "bigFont.fnt");
        label->setScale(0.4f); 

        auto toggleBtn = CCMenuItemSpriteExtra::create(
            label,
            this,
            menu_selector(MyPauseLayer::onToggleGhost)
        );
        
        toggleBtn->setPosition({30.0f, 30.0f});
        toggleBtn->setID("ghost-guide-toggle"_spr);

        menu->addChild(toggleBtn);
        menu->updateLayout();

        return true;
    }

    void onToggleGhost(CCObject* sender) {
        g_showGhost = !g_showGhost;
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto label = static_cast<CCLabelBMFont*>(btn->getNormalImage());
        if (g_showGhost) {
            label->setString("Ghost: ON");
        } else {
            label->setString("Ghost: OFF");
        }
    }
};

// ==========================================
// 2. THE PLAYLAYER LOGIC (Auto-Spawning)
// ==========================================
class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        PlayerObject* m_ghostBot = nullptr;
    };
    
    bool init(GJGameLevel* level, bool useReplay, bool dontRunActions) {
        if (!PlayLayer::init(level, useReplay, dontRunActions)) return false;
        m_fields->m_ghostBot = nullptr; 
        return true;
    }

    void onExit() {
        PlayLayer::onExit();
        m_fields->m_ghostBot = nullptr; 
    }

    void spawnGhostBot() {
        if (!m_fields->m_ghostBot) {
            // Draws directly onto PlayLayer root canvas
            m_fields->m_ghostBot = PlayerObject::create(1, 2, this, this, true);
            if (m_fields->m_ghostBot) {
                m_fields->m_ghostBot->setOpacity(128); 
                this->addChild(m_fields->m_ghostBot, 999);
            }
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel(); 
        
        if (g_showGhost) {
            spawnGhostBot();
            if (m_fields->m_ghostBot && this->m_player1) {
                m_fields->m_ghostBot->setPosition(this->m_player1->getPosition());
                m_fields->m_ghostBot->setVisible(true);
            }
        }
    }

    void update(float dt) {
        PlayLayer::update(dt); 

        if (g_showGhost) {
            if (!m_fields->m_ghostBot) {
                spawnGhostBot();
            }

            if (m_fields->m_ghostBot && this->m_player1) {
                m_fields->m_ghostBot->setVisible(true);
                
                float currentX = this->m_player1->getPositionX();
                float currentY = this->m_player1->getPositionY();
                
                // Force it to float slightly ahead of you so you can clearly see it rendering
                m_fields->m_ghostBot->setPosition({currentX + 60.0f, currentY + 20.0f}); 
                m_fields->m_ghostBot->setRotation(this->m_player1->getRotation()); 
            }
        } else {
            if (m_fields->m_ghostBot) {
                m_fields->m_ghostBot->setVisible(false);
            }
        }
    }
};