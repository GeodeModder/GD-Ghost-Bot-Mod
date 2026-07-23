#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include "GhostDebug.hpp"
#include "GhostPopups.hpp"
#include "GhostAccess.hpp"

using namespace geode::prelude;

// =====================================================================
// PAUSE LAYER HOOK - opens the Ghost Manager popup
// =====================================================================
class $modify(GhostPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("left-button-menu");
        if (!menu) menu = this->getChildByID("center-button-menu");
        if (!menu) return;

        auto ghostSprite = CCSprite::createWithSpriteFrameName("GJ_profileButton_001.png");
        auto ghostBtn = CCMenuItemSpriteExtra::create(ghostSprite, this, menu_selector(GhostPauseLayer::onOpenGhostManager));
        menu->addChild(ghostBtn);
        menu->updateLayout();
    }

    void onOpenGhostManager(CCObject*) {
        auto manager = ghostGetCurrentManager();
        if (!manager) {
            GhostDebug::popup("Ghost Debug ERROR", "Could not get current PlayLayer");
            return;
        }

        auto popup = GhostPopup::create(manager);
        popup->show();
    }
};
