#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <vector>
#include <fstream>
#include <filesystem>

using namespace geode::prelude;

// Struct to store individual frame data
struct GhostFrame {
    float x;
    float y;
    float rotation;
};

// Custom fields to keep track of per-level data cleanly
class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<GhostFrame> m_recordedFrames;
        std::vector<size_t> m_checkpointFrameCounts;
        std::vector<GhostFrame> m_playbackFrames;
        size_t m_currentFrameIndex = 0;
        cocos2d::CCSprite* m_ghostSprite = nullptr;
        bool m_hasGhost = false;
    };

    // 1. Initialize and try to load existing ghost data
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;

        m_fields->m_recordedFrames.clear();
        m_fields->m_checkpointFrameCounts.clear();
        m_fields->m_playbackFrames.clear();
        m_fields->m_currentFrameIndex = 0;

        // Load ghost file if it exists
        int levelID = level->m_levelID;
        auto savePath = Mod::get()->getModSettingSaveDir() / (std::to_string(levelID) + ".json");
        
        if (std::filesystem::exists(savePath)) {
            std::ifstream file(savePath);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();

                auto parseResult = matjson::parse(buffer.str());
                if (parseResult.has_value() && parseResult.value().contains("frames")) {
                    auto framesArray = parseResult.value()["frames"].as_array();
                    for (const auto& f : framesArray) {
                        GhostFrame frame;
                        frame.x = f["x"].as_double();
                        frame.y = f["y"].as_double();
                        frame.rotation = f["rotation"].as_double();
                        m_fields->m_playbackFrames.push_back(frame);
                    }
                    m_fields->m_hasGhost = !m_fields->m_playbackFrames.empty();
                }
            }
        }

        // Setup the physical Ghost Sprite if playback data exists
        if (m_fields->m_hasGhost) {
            m_fields->m_ghostSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            if (m_fields->m_ghostSprite) {
                // Fetch saved color preference (defaults to cyan/white mixed)
                int r = Mod::get()->getSavedValue<int>("color_r_" + std::to_string(levelID), 0);
                int g = Mod::get()->getSavedValue<int>("color_g_" + std::to_string(levelID), 255);
                int b = Mod::get()->getSavedValue<int>("color_b_" + std::to_string(levelID), 255);
                
                m_fields->m_ghostSprite->setColor({static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b)});
                m_fields->m_ghostSprite->setOpacity(150); // Make it slightly transparent
                m_objectLayer->addChild(m_fields->m_ghostSprite);
            }
        }

        return true;
    }

    // 2. Continuous frame recorder & playback animator
    void update(float dt) {
        PlayLayer::update(dt);

        // RECORDING SYSTEM
        if (this->m_isPracticeMode && this->m_player1) {
            GhostFrame currentFrame = {
                this->m_player1->m_position.x,
                this->m_player1->m_position.y,
                this->m_player1->getRotation()
            };
            m_fields->m_recordedFrames.push_back(currentFrame);
        }

        // PLAYBACK SYSTEM
        if (m_fields->m_hasGhost && m_fields->m_ghostSprite) {
            if (m_fields->m_currentFrameIndex < m_fields->m_playbackFrames.size()) {
                auto& frame = m_fields->m_playbackFrames[m_fields->m_currentFrameIndex];
                m_fields->m_ghostSprite->setPosition({frame.x, frame.y});
                m_fields->m_ghostSprite->setRotation(frame.rotation);
                m_fields->m_currentFrameIndex++;
            } else {
                m_fields->m_ghostSprite->setVisible(false); // Hide if macro ends early
            }
        }
    }
};
// Continuing the MyPlayLayer implementation...
class $modify(MyPlayLayerEngine, PlayLayer) {
    // 3. Keep track of vector size when a checkpoint is placed
    void createCheckpoint() {
        PlayLayer::createCheckpoint();
        auto customFields = reinterpret_cast<MyPlayLayer*>(this)->m_fields.value();
        customFields->m_checkpointFrameCounts.push_back(customFields->m_recordedFrames.size());
    }

    // 4. Handle manual deletion of checkpoints
    void removeLastCheckpoint() {
        PlayLayer::removeLastCheckpoint();
        auto customFields = reinterpret_cast<MyPlayLayer*>(this)->m_fields.value();
        if (!customFields->m_checkpointFrameCounts.empty()) {
            customFields->m_checkpointFrameCounts.pop_back();
        }
    }

    // 5. STITCHING: Rewind recorded frames to the last valid checkpoint on death
    void loadFromCheckpoint() {
        PlayLayer::loadFromCheckpoint();
        auto customFields = reinterpret_cast<MyPlayLayer*>(this)->m_fields.value();
        
        // Reset playback frame index back to 0 (or match it to checkpoint if tracking perfectly)
        customFields->m_currentFrameIndex = 0; 
        if (customFields->m_ghostSprite) customFields->m_ghostSprite->setVisible(true);

        if (!customFields->m_checkpointFrameCounts.empty()) {
            size_t framesAtCheckpoint = customFields->m_checkpointFrameCounts.back();
            if (framesAtCheckpoint < customFields->m_recordedFrames.size()) {
                customFields->m_recordedFrames.erase(
                    customFields->m_recordedFrames.begin() + framesAtCheckpoint, 
                    customFields->m_recordedFrames.end()
                );
            }
        } else {
            customFields->m_recordedFrames.clear();
        }
    }

    // 6. Save when level is complete
    void levelComplete() {
        PlayLayer::levelComplete();
        auto customFields = reinterpret_cast<MyPlayLayer*>(this)->m_fields.value();
        
        if (!customFields->m_recordedFrames.empty()) {
            int levelID = this->m_level->m_levelID;
            auto savePath = Mod::get()->getModSettingSaveDir() / (std::to_string(levelID) + ".json");
            
            matjson::Value jsonOutput = matjson::Object();
            matjson::Value jsonFrames = matjson::Array();
            
            for (const auto& frame : customFields->m_recordedFrames) {
                matjson::Value f = matjson::Object();
                f["x"] = frame.x;
                f["y"] = frame.y;
                f["rotation"] = frame.rotation;
                jsonFrames.push_back(f);
            }
            
            jsonOutput["frames"] = jsonFrames;
            
            std::ofstream file(savePath);
            if (file.is_open()) {
                file << jsonOutput.dump();
                file.close();
            }
        }
    }
};
// Custom Geode Popup to view and manage recorded ghost files
class GhostManagerPopup : public FLAlertLayer, public FLAlertLayerProtocol {
public:
    cocos2d::CCLayer* m_listLayer;

    static GhostManagerPopup* create() {
        auto ret = new GhostManagerPopup();
        if (ret && ret->init(240, 200, "GJ_square01.png", "Ghost Manager")) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init(float width, float height, const char* bg, const char* title) {
        if (!FLAlertLayer::init()) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        // Background touch blocker
        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);

        auto bgSprite = CCScale9Sprite::create(bg);
        bgSprite->setContentSize({width, height});
        bgSprite->setPosition(winSize / 2);
        this->addChild(bgSprite);

        auto titleLabel = CCLabelBMFont::create(title, "bigFont.fnt");
        titleLabel->setPosition({winSize.width / 2, winSize.height / 2 + height / 2 - 20});
        titleLabel->setScale(0.6f);
        this->addChild(titleLabel);

        // Close Button
        auto closeSprite = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        auto closeBtn = CCMenuItemSpriteExtra::create(closeSprite, this, menu_selector(GhostManagerPopup::onClose));
        auto menu = CCMenu::create(closeBtn, nullptr);
        menu->setPosition({winSize.width / 2 + width / 2 - 15, winSize.height / 2 + height / 2 - 15});
        this->addChild(menu);

        m_listLayer = CCLayer::create();
        this->setupList(width, height);

        return true;
    }

    void setupList(float w, float h) {
        m_listLayer->removeAllChildrenWithCleanup(true);
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto itemMenu = CCMenu::create();
        itemMenu->setPosition({0, 0});

        auto dir = Mod::get()->getModSettingSaveDir();
        float yOffset = winSize.height / 2 + h / 2 - 50;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();

                // Name label
                auto nameLbl = CCLabelBMFont::create(filename.c_str(), "goldFont.fnt");
                nameLbl->setPosition({winSize.width / 2, yOffset});
                nameLbl->setScale(0.5f);
                this->addChild(nameLbl);

                // LEFT: Delete Button
                auto delSprite = CCSprite::createWithSpriteFrameName("edit_delBtnSmall_001.png");
                auto delBtn = CCMenuItemSpriteExtra::create(delSprite, this, menu_selector(GhostManagerPopup::onDeleteGhost));
                delBtn->setPosition({winSize.width / 2 - w / 2 + 30, yOffset});
                delBtn->setID(filename); // Pass level ID via Node ID
                itemMenu->addChild(delBtn);

                // RIGHT: Color cycle button 
                auto colorSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
                colorSprite->setScale(0.6f);
                auto colorBtn = CCMenuItemSpriteExtra::create(colorSprite, this, menu_selector(GhostManagerPopup::onCycleColor));
                colorBtn->setPosition({winSize.width / 2 + w / 2 - 30, yOffset});
                colorBtn->setID(filename);
                itemMenu->addChild(colorBtn);

                yOffset -= 35;
            }
        }
        this->addChild(itemMenu);
    }

    void onDeleteGhost(CCObject* sender) {
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        std::string levelID = btn->getID();
        auto path = Mod::get()->getModSettingSaveDir() / (levelID + ".json");
        std::filesystem::remove(path);
        
        // Refresh display
        this->setupList(240, 200);
    }

    void onCycleColor(CCObject* sender) {
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        std::string levelID = btn->getID();
        
        // Cycles color states between Cyan -> Red -> Green -> Yellow
        int curColor = Mod::get()->getSavedValue<int>("color_state_" + levelID, 0);
        curColor = (curColor + 1) % 4;
        Mod::get()->setSavedValue<int>("color_state_" + levelID, curColor);

        int r = 0, g = 255, b = 255;
        if (curColor == 1) { r = 255; g = 0; b = 0; }       // Red
        else if (curColor == 2) { r = 0; g = 255; b = 0; }  // Green
        else if (curColor == 3) { r = 255; g = 255; b = 0; }// Yellow

        Mod::get()->setSavedValue<int>("color_r_" + levelID, r);
        Mod::get()->setSavedValue<int>("color_g_" + levelID, g);
        Mod::get()->setSavedValue<int>("color_b_" + levelID, b);
        
        FLAlertLayer::create("Success", "Ghost color updated for next attempt!", "OK")->show();
    }

    void onClose(CCObject*) {
        this->removeFromParentAndCleanup(true);
    }
};

// Hook Pause UI to easily launch the manager popup
class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        
        auto menu = this->getChildByID("left-button-menu");
        if (!menu) menu = this->getChildByID("center-button-menu");

        if (menu) {
            auto ghostSprite = CCSprite::createWithSpriteFrameName("GJ_profileButton_001.png"); // Placeholder icon
            auto ghostBtn = CCMenuItemSpriteExtra::create(ghostSprite, this, menu_selector(MyPauseLayer::onOpenGhostManager));
            menu->addChild(ghostBtn);
            menu->updateLayout();
        }
    }

    void onOpenGhostManager(CCObject*) {
        auto popup = GhostManagerPopup::create();
        popup->show();
    }
};
