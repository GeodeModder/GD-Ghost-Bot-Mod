#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// This global variable will control whether our ghost is active or hidden
bool g_showGhost = false;

class $modify(MyPauseLayer, PauseLayer) {
    
    // This runs right when the pause menu is being created
    void customSetup() {
        // Run the original game setup first so the buttons actually exist
        PauseLayer::customSetup();

        // Find the standard right-side menu or center menu where buttons live
        auto menu = this->getChildByID("right-side-menu");
        if (!menu) {
            menu = this->getChildByID("center-menu");
        }

        if (menu) {
            // Create a clean text label that shows the current state
            auto label = CCLabelBMFont::create(g_showGhost ? "Ghost: ON" : "Ghost: OFF", "bigFont.fnt");
            label->setScale(0.5f);

            // Turn that label into a clickable button!
            auto toggleBtn = CCMenuItemSpriteExtra::create(
                label,
                this,
                menu_selector(MyPauseLayer::onToggleGhost)
            );
            toggleBtn->setID("ghost-guide-toggle"_spr);

            // Add it to the menu and tell Geode to fix the alignment automatically
            menu->addChild(toggleBtn);
            menu->updateLayout();
        }
    }

    // This function triggers the exact millisecond you click the button
    void onToggleGhost(CCObject* sender) {
        g_showGhost = !g_showGhost;

        // Cast the sender back to a button so we can change its label text dynamically
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        auto label = static_cast<CCLabelBMFont*>(btn->getNormalImage());

        if (g_showGhost) {
            label->setString("Ghost: ON");
            // Show a quick in-game notification popup!
            FLAlertLayer::create("Ghost Bot", "Ghost Guide <g>Enabled</g>!", "OK")->show();
        } else {
            label->setString("Ghost: OFF");
            FLAlertLayer::create("Ghost Bot", "Ghost Guide <r>Disabled</r>!", "OK")->show();
        }
    }
};
