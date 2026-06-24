#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

struct Frame { float x, y, rot; };
static std::vector<std::vector<Frame>> g_segments;
static std::vector<Frame> g_currentSegment;

// ==========================================
// 🏗️ SEGMENTED RECORDING ENGINE
// ==========================================
struct $modify(GhostPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool usePractice, bool isPlatformer) {
        if (!PlayLayer::init(level, usePractice, isPlatformer)) return false;
        g_segments.clear();
        g_currentSegment.clear();
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (m_player1 && !m_player1->m_isDead) {
            g_currentSegment.push_back({m_player1->getPositionX(), m_player1->getPositionY(), m_player1->getRotation()});
        }
    }

    void checkpointActivated(CheckpointObject* obj) {
        PlayLayer::checkpointActivated(obj);
        g_segments.push_back(g_currentSegment);
        g_currentSegment.clear();
    }

    void resetLevel() {
        if (m_isPracticeMode) g_currentSegment.clear();
        PlayLayer::resetLevel();
    }
};

// ==========================================
// 🎛️ UI & FILE MANAGEMENT
// ==========================================
class GhostPopup : public FLAlertLayer {
    int m_levelID;

    bool init(int levelID) {
        if (!FLAlertLayer::init(nullptr, "Ghost Manager", "OK", nullptr, 350.f)) return false;
        m_levelID = levelID;
        
        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        m_mainLayer->addChild(menu);

        // List files in save dir
        auto saveDir = Mod::get()->getSaveDir();
        int i = 0;
        for (auto const& entry : std::filesystem::directory_iterator(saveDir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find(std::to_string(levelID) + "_") != std::string::npos) {
                auto btn = CCMenuItemSpriteExtra::create(
                    ButtonSprite::create(filename.c_str(), "goldFont.fnt", "GJ_button_01.png"), 
                    this, menu_selector(GhostPopup::onGhostSelected));
                btn->setUserData(new std::string(entry.path().string()));
                btn->setPosition({0, 80.f - (i * 40.f)});
                menu->addChild(btn);
                i++;
            }
        }
        return true;
    }

    void onGhostSelected(CCObject* sender) {
        auto path = static_cast<std::string*>(static_cast<CCMenuItem*>(sender)->getUserData());
        auto alert = FLAlertLayer::create("Options", "Delete this ghost?", "No", "Yes", 200.f);
        alert->setCallback([path](CCObject* obj) {
            if (static_cast<FLAlertLayer*>(obj)->btn1Selected()) { // User clicked 'Yes'
                std::filesystem::remove(*path);
            }
            delete path;
        });
        alert->show();
    }

public:
    static void show(int id) {
        auto p = new GhostPopup();
        if (p && p->init(id)) p->autorelease()->show();
    }
};

// ==========================================
// ⏸️ PAUSE MENU
// ==========================================
struct $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = this->getChildByID("right-button-menu");
        if (menu) {
            auto btn = CCMenuItemSpriteExtra::create(
                cocos2d::CCSprite::createWithSpriteFrameName("GJ_downloadsIcon_001.png"), 
                this, menu_selector(MyPauseLayer::onOpen));
            menu->addChild(btn);
            menu->updateLayout();
        }
    }
    void onOpen(CCObject*) {
        if (auto pl = PlayLayer::get()) GhostPopup::show(pl->m_level->m_levelID);
    }
};
