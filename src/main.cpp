#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/CheckpointGameObject.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using namespace geode::prelude;

// ==========================================
// 🏗️ GHOST DATA STRUCTURES
// ==========================================
struct Frame { float x, y, rot; };
static std::vector<std::vector<Frame>> g_segments;
static std::vector<Frame> g_currentSegment;

// ==========================================
// 🕹️ SEGMENTED RECORDING ENGINE
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

    void checkpointActivated(CheckpointGameObject* obj) {
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
// 🎛️ UI & CONFIRMATION LOGIC
// ==========================================
class GhostPopup : public FLAlertLayer, public FLAlertLayerProtocol {
    int m_levelID;

    void FLAlert_Clicked(FLAlertLayer* btn, bool btn2) override {
        if (btn2) { 
            auto pathPtr = static_cast<std::string*>(btn->getUserData());
            if (pathPtr) {
                std::filesystem::remove(*pathPtr);
                delete pathPtr; 
            }
        } else {
            auto pathPtr = static_cast<std::string*>(btn->getUserData());
            delete pathPtr;
        }
    }

    bool init(int levelID) {
        // FIXED: Added all 9 arguments required by the Geode binding
        // (Delegate, Title, Desc, Btn1, Btn2, Width, Scroll, Height, TextScale)
        if (!FLAlertLayer::init(this, "Ghost Manager", "Select ghost to delete", "OK", nullptr, 350.f, false, 200.f, 1.f)) return false;
        
        m_levelID = levelID;
        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        m_mainLayer->addChild(menu);

        auto saveDir = Mod::get()->getSaveDir();
        int i = 0;
        for (auto const& entry : std::filesystem::directory_iterator(saveDir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find(std::to_string(levelID) + "_") != std::string::npos) {
                auto pathCopy = new std::string(entry.path().string());
                auto btn = CCMenuItemSpriteExtra::create(
                    ButtonSprite::create(filename.c_str(), "goldFont.fnt", "GJ_button_01.png"), 
                    this, menu_selector(GhostPopup::onGhostSelected));
                btn->setUserData(pathCopy); 
                btn->setPosition({0, 80.f - (i * 40.f)});
                menu->addChild(btn);
                i++;
            }
        }
        return true;
    }

    void onGhostSelected(CCObject* sender) {
        auto pathPtr = static_cast<std::string*>(static_cast<CCMenuItem*>(sender)->getUserData());
        auto alert = FLAlertLayer::create(this, "Delete?", "Are you sure you want to delete this ghost?", "No", "Yes");
        alert->setUserData(new std::string(*pathPtr)); 
        alert->show();
    }

public:
    // FIXED: Renamed to 'open' to avoid collision with FLAlertLayer::show()
    static void open(int id) {
        auto p = new GhostPopup();
        if (p && p->init(id)) {
            p->autorelease();
            p->show();
        }
    }
};

// ==========================================
// ⏸️ PAUSE MENU HOOK
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
        if (auto pl = PlayLayer::get()) GhostPopup::open(pl->m_level->m_levelID);
    }
};
