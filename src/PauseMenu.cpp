#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Global control variables
bool g_showGhost = false;
static PlayerObject* g_ghostPlayer = nullptr; 

// ==========================================
// 1. THE UI CONTROL
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
// 2. THE CORRECTED GHOST LOGIC
// ==========================================
class $modify(MyPlayLayer, PlayLayer) {
    
    virtual void onExit() {
        PlayLayer::onExit();
        g_ghostPlayer = nullptr; 
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontRunActions) {
        if (!PlayLayer::init(level, useReplay, dontRunActions)) return false;
        
        g_ghostPlayer = nullptr; 

        if (g_showGhost) {
            // FIX: Passing 'this' as both the GJBaseGameLayer and CCLayer parameters to resolve type casting
            g_ghostPlayer = PlayerObject::create(1, 1, this, this, false);
            if (g_ghostPlayer) {
                g_ghostPlayer->setOpacity(128); 
                this->addChild(g_ghostPlayer, 999);
            }
        }
        return true;
    }

    virtual void resetLevel() {
        PlayLayer::resetLevel(); 
        
        if (g_showGhost && g_ghostPlayer) {
            g_ghostPlayer->setPosition(this->m_player1->getPosition());
        }
    }

    virtual void update(float dt) {
        PlayLayer::update(dt); 

        // FIX: Checking m_isDead via the player1 object context rather than the parent layer
        if (g_showGhost && g_ghostPlayer && this->m_player1 && !this->m_player1->m_isDead) {
            float currentX = this->m_player1->getPositionX();
            float currentY = this->m_player1->getPositionY();
            
            // Render a clear visual lead offset ahead of the player position
            g_ghostPlayer->setPosition({currentX + 100.0f, currentY}); 
            g_ghostPlayer->setRotation(0); 
        }
    }

    virtual void destroyPlayer(PlayerObject* player, GameObject* obstacle) {
        if (g_showGhost && g_ghostPlayer && player == this->m_player1) {
            PlayLayer::destroyPlayer(g_ghostPlayer, obstacle);
        }
        PlayLayer::destroyPlayer(player, obstacle);
    }
};
