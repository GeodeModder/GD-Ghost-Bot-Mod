#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp> // 💡 FIX: Added missing popup UI framework header
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <sstream>
#include <ctime>

using namespace geode::prelude;

// Core physics frame tracker
struct GhostFrame {
    cocos2d::CCPoint position;
    float rotation;
    bool isUpsideDown;
};

// Custom user identity container 
struct CustomGhostProfile {
    std::string name;
    int r = 0, g = 255, b = 255; 
    std::vector<GhostFrame> frames;
};

// JSON Serializers for Geode File Engine
template <>
struct matjson::Serialize<GhostFrame> {
    static geode::Result<GhostFrame> fromJson(matjson::Value const& value) {
        GEODE_UNWRAP_INTO(double x, value["x"].asDouble());
        GEODE_UNWRAP_INTO(double y, value["y"].asDouble());
        GEODE_UNWRAP_INTO(double r, value["r"].asDouble());
        GEODE_UNWRAP_INTO(bool u, value["u"].asBool());

        return geode::Ok(GhostFrame {
            .position = cocos2d::CCPoint(static_cast<float>(x), static_cast<float>(y)),
            .rotation = static_cast<float>(r),
            .isUpsideDown = u
        });
    }
    static matjson::Value toJson(GhostFrame const& value) {
        auto obj = matjson::Value();
        obj["x"] = static_cast<double>(value.position.x);
        obj["y"] = static_cast<double>(value.position.y);
        obj["r"] = static_cast<double>(value.rotation);
        obj["u"] = value.isUpsideDown;
        return obj;
    }
};

template <>
struct matjson::Serialize<CustomGhostProfile> {
    static geode::Result<CustomGhostProfile> fromJson(matjson::Value const& value) {
        GEODE_UNWRAP_INTO(std::string name, value["name"].asString());
        GEODE_UNWRAP_INTO(int r, value["r"].asInt());
        GEODE_UNWRAP_INTO(int g, value["g"].asInt());
        GEODE_UNWRAP_INTO(int b, value["b"].asInt());
        GEODE_UNWRAP_INTO(std::vector<GhostFrame> frames, value["frames"].as<std::vector<GhostFrame>>());

        return geode::Ok(CustomGhostProfile { name, r, g, b, frames });
    }
    static matjson::Value toJson(CustomGhostProfile const& value) {
        auto obj = matjson::Value();
        obj["name"] = value.name;
        obj["r"] = value.r;
        obj["g"] = value.g;
        obj["b"] = value.b;
        obj["frames"] = value.frames;
        return obj;
    }
};

// Shared cache across layout boundaries
static std::vector<GhostFrame> g_lastAttemptData;

struct LiveGhostTrack {
    CustomGhostProfile profile;
    cocos2d::CCSprite* sprite = nullptr;
    std::string filePath;
};

// ==========================================
// 🕹️ HOOK: INDEPENDENT PLAYBACK & RENDERING
// ==========================================
struct $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<GhostFrame> m_liveRecording;
        std::vector<LiveGhostTrack> m_activeGhosts;
        int m_levelID = 0;
        int m_frameCounter = 0; // Independent master playback clock
    };

    bool init(GJGameLevel* level, bool usePractice, bool isPlatformer) {
        if (!PlayLayer::init(level, usePractice, isPlatformer)) return false;

        m_fields->m_levelID = level->m_levelID;
        m_fields->m_liveRecording.clear();
        m_fields->m_frameCounter = 0;
        
        this->assembleActiveGhosts();
        return true;
    }

    void assembleActiveGhosts() {
        m_fields->m_activeGhosts.clear();
        auto saveDir = geode::Mod::get()->getSaveDir();
        std::filesystem::create_directories(saveDir);

        std::string prefix = "ghost_" + std::to_string(m_fields->m_levelID) + "_";
        
        for (auto const& entry : std::filesystem::directory_iterator(saveDir)) {
            std::string filename = entry.path().filename().string();
            if (filename.rfind(prefix, 0) == 0 && filename.substr(filename.find_last_of('.')) == ".json") {
                try {
                    std::ifstream file(entry.path());
                    std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    auto json = matjson::parse(str);
                    if (json) {
                        auto profileRes = json.unwrap().as<CustomGhostProfile>();
                        if (profileRes) {
                            LiveGhostTrack track;
                            track.profile = profileRes.unwrap();
                            track.filePath = entry.path().string();
                            track.sprite = nullptr; // Initialized lazily inside update loop
                            m_fields->m_activeGhosts.push_back(track);
                        }
                    }
                } catch(...) {}
            }
        }
    }

    void update(float dt) {
        PlayLayer::update(dt);

        // Advance master layout clock unconditionally so ghosts never freeze on death
        m_fields->m_frameCounter++;

        // Append active tracking frames only while player is physically alive
        if (m_player1 && !m_player1->m_isDead) {
            GhostFrame currentFrame {
                .position = m_player1->getPosition(),
                .rotation = m_player1->getRotation(),
                .isUpsideDown = m_player1->m_isUpsideDown
            };
            m_fields->m_liveRecording.push_back(currentFrame);
        }

        size_t playbackIndex = static_cast<size_t>(m_fields->m_frameCounter);

        // Process rendering channels for all registered ghosts
        for (auto& ghost : m_fields->m_activeGhosts) {
            // Lazy asset initialization layer
            if (!ghost.sprite && m_objectLayer) {
                ghost.sprite = cocos2d::CCSprite::createWithSpriteFrameName("square02_001.png");
                if (ghost.sprite) {
                    ghost.sprite->setColor({
                        static_cast<unsigned char>(ghost.profile.r),
                        static_cast<unsigned char>(ghost.profile.g),
                        static_cast<unsigned char>(ghost.profile.b)
                    });
                    ghost.sprite->setOpacity(150);
                    ghost.sprite->setScale(0.55f);
                    ghost.sprite->setVisible(false);
                    m_objectLayer->addChild(ghost.sprite, 2000); // High Z-Order rendering
                }
            }

            // Animate frames continuously regardless of player state
            if (ghost.sprite && playbackIndex < ghost.profile.frames.size()) {
                auto const& f = ghost.profile.frames[playbackIndex];
                ghost.sprite->setPosition(f.position);
                ghost.sprite->setRotation(f.rotation);
                ghost.sprite->setScaleY(f.isUpsideDown ? -0.55f : 0.55f);
                ghost.sprite->setVisible(true);
            } else if (ghost.sprite) {
                ghost.sprite->setVisible(false);
            }
        }
    }

    void resetLevel() {
        // Cache the full data package right before wiping memory
        if (m_fields->m_liveRecording.size() > 15) { 
            g_lastAttemptData = m_fields->m_liveRecording;
        }
        
        PlayLayer::resetLevel();
        m_fields->m_liveRecording.clear();
        m_fields->m_frameCounter = 0; // Reset master tracking clock
        
        for (auto& ghost : m_fields->m_activeGhosts) {
            if (ghost.sprite) ghost.sprite->setVisible(false);
        }
    }
};

// ==========================================
// 🎛️ UI: PAUSE MENU CONTROL CONSOLE
// ==========================================
class AdvancedGhostPopup : public geode::Popup<int>, public TextInputDelegate {
protected:
    int m_levelID;
    CCTextInputNode* m_inputField = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

    bool setup(int levelID) override {
        m_levelID = levelID;
        this->setTitle("Custom Ghost Control Room");

        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        
        auto menu = cocos2d::CCMenu::create();
        menu->setPosition({0, 0});
        m_mainLayer->addChild(menu);

        // Naming Field Setup
        m_inputField = CCTextInputNode::create(180.f, 30.f, "Name your ghost...", "bigFont.fnt");
        m_inputField->setPosition({winSize.width / 2, winSize.height / 2 + 35});
        m_inputField->setAllowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ");
        m_inputField->setMaxLabelLength(16);
        m_mainLayer->addChild(m_inputField);

        // Frame Status Label
        std::string statStr = g_lastAttemptData.empty() ? "No run data cached." : "Cached Run: " + std::to_string(g_lastAttemptData.size()) + " frames";
        m_statusLabel = cocos2d::CCLabelBMFont::create(statStr.c_str(), "goldFont.fnt");
        m_statusLabel->setScale(0.35f);
        m_statusLabel->setPosition({winSize.width / 2, winSize.height / 2 + 5});
        m_mainLayer->addChild(m_statusLabel);

        // --- 🎨 THE MULTI-COLOR BUTTON ROW ---
        // 💡 FIX: Added required font param to correctly match Geode ButtonSprite::create methods
        float startX = winSize.width / 2 - 100.f;
        float spacing = 50.f;
        float yPos = winSize.height / 2 - 35.f;

        // 1. Cyan Button
        auto cBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("CYN", "goldFont.fnt", "GJ_button_01.png"), this, menu_selector(AdvancedGhostPopup::onCyan));
        cBtn->setPosition({startX + (0 * spacing), yPos});
        cBtn->setScale(0.7f);
        menu->addChild(cBtn);

        // 2. Gold Button
        auto gBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("GLD", "goldFont.fnt", "goldButton_001.png"), this, menu_selector(AdvancedGhostPopup::onGold));
        gBtn->setPosition({startX + (1 * spacing), yPos});
        gBtn->setScale(0.7f);
        menu->addChild(gBtn);

        // 3. Red Button
        auto rBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("RED", "goldFont.fnt", "GJ_button_06.png"), this, menu_selector(AdvancedGhostPopup::onRed));
        rBtn->setPosition({startX + (2 * spacing), yPos});
        rBtn->setScale(0.7f);
        menu->addChild(rBtn);

        // 4. Green Button
        auto grBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("GRN", "goldFont.fnt", "GJ_button_02.png"), this, menu_selector(AdvancedGhostPopup::onGreen));
        grBtn->setPosition({startX + (3 * spacing), yPos});
        grBtn->setScale(0.7f);
        menu->addChild(grBtn);

        // 5. Purple Button
        auto pBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("PRP", "goldFont.fnt", "GJ_button_04.png"), this, menu_selector(AdvancedGhostPopup::onPurple));
        pBtn->setPosition({startX + (4 * spacing), yPos});
        pBtn->setScale(0.7f);
        menu->addChild(pBtn);

        return true;
    }

    void saveWithColor(int r, int g, int b) {
        if (g_lastAttemptData.empty()) {
            FLAlertLayer::create("Error", "No run data available! Complete a run first.", "OK")->show();
            return;
        }

        std::string ghostName = m_inputField->getString();
        if (ghostName.empty()) ghostName = "Custom Ghost";

        CustomGhostProfile prof{ ghostName, r, g, b, g_lastAttemptData };

        auto saveDir = geode::Mod::get()->getSaveDir();
        std::string filename = "ghost_" + std::to_string(m_levelID) + "_" + std::to_string(std::time(nullptr)) + ".json";
        
        std::ofstream file(saveDir / filename);
        matjson::Value json = prof;
        file << json.dump();
        file.close();

        FLAlertLayer::create("Saved!", "'" + ghostName + "' is ready to race! Restart level to apply.", "OK")->show();
        this->onClose(nullptr);
    }

    void onCyan(cocos2d::CCObject*) { saveWithColor(0, 255, 255); }
    void onGold(cocos2d::CCObject*) { saveWithColor(255, 215, 0); }
    void onRed(cocos2d::CCObject*) { saveWithColor(255, 60, 60); }
    void onGreen(cocos2d::CCObject*) { saveWithColor(60, 255, 60); }
    void onPurple(cocos2d::CCObject*) { saveWithColor(190, 60, 255); }

public:
    static AdvancedGhostPopup* create(int levelID) {
        auto ret = new AdvancedGhostPopup();
        if (ret && ret->initAnchored(290.f, 170.f, levelID)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

struct $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = this->getChildByID("center-button-menu");
        
        if (menu) {
            auto sprite = cocos2d::CCSprite::createWithSpriteFrameName("GJ_downloadsIcon_001.png");
            auto btn = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(MyPauseLayer::onOpenCustomGhostManager));
            btn->setID("ghost-manager-button"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }
    }

    void onOpenCustomGhostManager(cocos2d::CCObject*) {
        auto playLayer = PlayLayer::get();
        if (playLayer) {
            auto myPlayLayer = static_cast<MyPlayLayer*>(playLayer);
            
            // UX Feature: If player pauses MID-RUN, immediately clone current active path data!
            if (!myPlayLayer->m_fields->m_liveRecording.empty()) {
                g_lastAttemptData = myPlayLayer->m_fields->m_liveRecording;
            }
            AdvancedGhostPopup::create(playLayer->m_level->m_levelID)->show();
        }
    }
};
