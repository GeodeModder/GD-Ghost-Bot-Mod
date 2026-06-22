#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Simple global boolean tracking if the feature is toggled on or off
bool g_showGhost = false;

// ==========================================
// 1. THE UI CONTROL (Android Safe Signature)
// ==========================================
class $modify(MyPauseLayer, PauseLayer) {
    bool init(bool unfocused) {
        if (!PauseLayer::init(unfocused)) return false;

        // Safely fetch standard menus on the Android UI tree
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
// 2. THE MODERN 2.2081 GHOST LOGIC
// ==========================================
class $modify(MyPlayLayer, PlayLayer) {
    // 4. MEMORY ISOLATION: Declaring custom member variables securely via Geode Fields
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

    void resetLevel() {
        PlayLayer::resetLevel(); 
        
        if (g_showGhost) {
            if (!m_fields->m_ghostBot && this->m_objectLayer) {
                // 2. PLAYER INPUTS & MANIPULATION: Direct 2.2081 constructor signature
                m_fields->m_ghostBot = PlayerObject::create(1, 2, this->m_objectLayer, this, false);
                if (m_fields->m_ghostBot) {
                    m_fields->m_ghostBot->setOpacity(128); // Make our pace car semi-translucent 👻
                    this->m_objectLayer->addChild(m_fields->m_ghostBot, 999);
                }
            }
            
            if (m_fields->m_ghostBot && this->m_player1) {
                m_fields->m_ghostBot->setPosition(this->m_player1->getPosition());
                m_fields->m_ghostBot->setVisible(true);
            }
        } else {
            if (m_fields->m_ghostBot) {
                m_fields->m_ghostBot->setVisible(false);
            }
        }
    }

    void update(float dt) {
        PlayLayer::update(dt); 

        // Safely check variables using the modern memory-safe architecture
        if (g_showGhost && m_fields->m_ghostBot && this->m_player1) {
            float currentX = this->m_player1->getPositionX();
            float currentY = this->m_player1->getPositionY();
            
            // Positions the pace-car ghost slightly ahead of the real player
            m_fields->m_ghostBot->setPosition({currentX + 100.0f, currentY}); 
            m_fields->m_ghostBot->setRotation(this->m_player1->getRotation()); 
        }
    }
};
