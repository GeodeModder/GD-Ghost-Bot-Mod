#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <functional>
#include <string>
#include "GhostDebug.hpp"
#include "GhostTypes.hpp"

using namespace geode::prelude;

// =====================================================================
// GHOST NAME DIALOG
// Used both for naming a brand-new route and renaming an existing one.
// =====================================================================
class GhostNameDialog : public geode::Popup, public TextInputDelegate {
protected:
    std::function<void(const std::string&)> m_onConfirm;
    CCTextInputNode* m_input = nullptr;
    bool m_resultHandled = false; // guards against double-callback / stuck popup
    std::string m_defaultText;

    // geode::Popup::init(width, height) handles the background, close
    // button, and touch/keypad setup for us - no manual boilerplate needed.
    bool init(std::string const& defaultText) {
        if (!Popup::init(240.f, 120.f)) return false;
        m_defaultText = defaultText;

        this->setTitle("Save Route");

        m_input = CCTextInputNode::create(180, 30, "Route name", "chatFont.fnt");
        m_input->setString(defaultText);
        m_input->setDelegate(this);
        m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0, 10});

        auto confirmSprite = ButtonSprite::create("Save");
        auto confirmBtn = CCMenuItemSpriteExtra::create(confirmSprite, this, menu_selector(GhostNameDialog::onConfirm));

        auto cancelSprite = ButtonSprite::create("Cancel");
        auto cancelBtn = CCMenuItemSpriteExtra::create(cancelSprite, this, menu_selector(GhostNameDialog::onCancel));

        auto menu = CCMenu::create(cancelBtn, confirmBtn, nullptr);
        menu->setLayout(RowLayout::create()->setGap(20.f));
        menu->updateLayout();
        m_mainLayer->addChildAtPosition(menu, Anchor::Center, {0, -25});

        return true;
    }

public:
    static GhostNameDialog* create(const std::string& defaultText, std::function<void(const std::string&)> onConfirm) {
        auto ret = new GhostNameDialog();
        if (ret && ret->init(defaultText)) {
            ret->m_onConfirm = onConfirm;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onConfirm(CCObject*) {
        if (m_resultHandled) return; // prevents the "both buttons unresponsive" bug
        m_resultHandled = true;

        std::string text = m_input ? m_input->getString() : "";
        if (text.empty()) text = "Unnamed Route";

        this->removeFromParentAndCleanup(true);
        if (m_onConfirm) m_onConfirm(text);
    }

    void onCancel(CCObject*) {
        if (m_resultHandled) return; // same guard - fixes stuck-popup bug
        m_resultHandled = true;
        this->removeFromParentAndCleanup(true);
        // deliberately no callback - caller decides what "cancel" means
    }

    void textChanged(CCTextInputNode*) override {}
};

// =====================================================================
// GHOST POPUP (pause menu UI)
// =====================================================================
class GhostPopup : public geode::Popup {
protected:
    GhostManager* m_manager = nullptr;
    cocos2d::CCLayer* m_listLayer = nullptr;

    bool init(GhostManager* manager) {
        if (!Popup::init(300.f, 240.f)) return false;
        m_manager = manager;

        this->setTitle("Ghost Manager");

        auto recordSprite = ButtonSprite::create("Record New Route");
        auto recordBtn = CCMenuItemSpriteExtra::create(recordSprite, this, menu_selector(GhostPopup::onRecordNew));
        auto topMenu = CCMenu::create(recordBtn, nullptr);
        m_mainLayer->addChildAtPosition(topMenu, Anchor::Top, {0, -35});

        // Live status label - lets you check recording progress just by
        // pausing and opening this popup, no gameplay-interrupting popup needed.
        std::string statusText = manager->m_isRecording
            ? ("Recording: " + std::to_string(manager->m_recordingBuffer.size()) + " frames captured so far")
            : "Not currently recording";
        auto statusLbl = CCLabelBMFont::create(statusText.c_str(), "chatFont.fnt");
        statusLbl->setScale(0.5f);
        m_mainLayer->addChildAtPosition(statusLbl, Anchor::Top, {0, -60});

        // Popup base already provides a close button in the corner - no
        // need to add our own like the old FLAlertLayer version did.

        m_listLayer = CCLayer::create();
        m_mainLayer->addChild(m_listLayer);

        refreshList();
        return true;
    }

public:
    static GhostPopup* create(GhostManager* manager) {
        auto ret = new GhostPopup();
        if (ret && ret->init(manager)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void refreshList() {
        m_listLayer->removeAllChildrenWithCleanup(true);
        if (!m_manager) return;

        auto itemMenu = CCMenu::create();
        itemMenu->setPosition({0, 0});

        float yOffset = 55;

        for (auto& ghost : m_manager->m_loadedGhosts) {
            auto colorSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
            colorSprite->setScale(0.5f);
            auto colorBtn = CCMenuItemSpriteExtra::create(colorSprite, this, menu_selector(GhostPopup::onCycleColor));
            colorBtn->setPosition({-125, yOffset});
            colorBtn->setID(ghost.filename);
            itemMenu->addChild(colorBtn);

            auto nameLbl = CCLabelBMFont::create(ghost.routeName.c_str(), "goldFont.fnt");
            nameLbl->setScale(0.45f);
            nameLbl->setAnchorPoint({0, 0.5f});
            m_listLayer->addChildAtPosition(nameLbl, Anchor::Center, {-95, yOffset});

            auto toggleSprite = ButtonSprite::create(ghost.enabled ? "On" : "Off");
            auto toggleBtn = CCMenuItemSpriteExtra::create(toggleSprite, this, menu_selector(GhostPopup::onToggle));
            toggleBtn->setPosition({30, yOffset});
            toggleBtn->setID(ghost.filename);
            itemMenu->addChild(toggleBtn);

            auto renameSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
            renameSprite->setScale(0.5f);
            auto renameBtn = CCMenuItemSpriteExtra::create(renameSprite, this, menu_selector(GhostPopup::onRename));
            renameBtn->setPosition({75, yOffset});
            renameBtn->setID(ghost.filename);
            itemMenu->addChild(renameBtn);

            auto delSprite = CCSprite::createWithSpriteFrameName("edit_delBtnSmall_001.png");
            auto delBtn = CCMenuItemSpriteExtra::create(delSprite, this, menu_selector(GhostPopup::onDelete));
            delBtn->setPosition({125, yOffset});
            delBtn->setID(ghost.filename);
            itemMenu->addChild(delBtn);

            yOffset -= 30;
        }

        itemMenu->setPosition({150, 120}); // offset to popup center since m_listLayer sits at (0,0)

        m_listLayer->addChild(itemMenu);
    }

    void onRecordNew(CCObject*) {
        GhostDebug::popup("Ghost Debug", "Record New Route button was pressed");
        if (!m_manager) {
            GhostDebug::popup("Ghost Debug ERROR", "m_manager is NULL inside GhostPopup");
            return;
        }
        m_manager->startRecording();
        this->removeFromParentAndCleanup(true);
    }

    void onToggle(CCObject* sender) {
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        std::string filename = btn->getID();
        for (auto& g : m_manager->m_loadedGhosts) {
            if (g.filename == filename) g.enabled = !g.enabled;
        }
        m_manager->saveMetadata();
        refreshList();
    }

    void onRename(CCObject* sender) {
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        std::string filename = btn->getID();

        std::string currentName;
        for (auto& g : m_manager->m_loadedGhosts) {
            if (g.filename == filename) currentName = g.routeName;
        }

        auto manager = m_manager;
        auto dialog = GhostNameDialog::create(currentName, [manager, filename](const std::string& newName) {
            manager->renameGhost(filename, newName);
        });
        dialog->show();
    }

    void onCycleColor(CCObject* sender) {
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        std::string filename = btn->getID();

        static const cocos2d::ccColor3B colors[] = {
            {255, 255, 255}, {255, 0, 0}, {0, 255, 0}, {0, 150, 255}, {255, 255, 0}
        };

        for (auto& g : m_manager->m_loadedGhosts) {
            if (g.filename == filename) {
                int curIndex = 0;
                for (int i = 0; i < 5; i++) {
                    if (g.color.r == colors[i].r && g.color.g == colors[i].g && g.color.b == colors[i].b) {
                        curIndex = i;
                        break;
                    }
                }
                g.color = colors[(curIndex + 1) % 5];
            }
        }
        m_manager->saveMetadata();
        refreshList();
    }

    void onDelete(CCObject* sender) {
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        m_manager->deleteGhost(btn->getID());
        refreshList();
    }

    void onClose(CCObject*) {
        this->removeFromParentAndCleanup(true);
    }
};
