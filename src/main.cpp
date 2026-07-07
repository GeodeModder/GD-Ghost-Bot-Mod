#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>

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
        bool m_isRecording = false;
        bool m_saveRequested = false;
    };

    // 1. Initialize and try to load existing ghost data
    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;

        auto fields = m_fields;
        fields->m_recordedFrames.clear();
        fields->m_checkpointFrameCounts.clear();
        fields->m_playbackFrames.clear();
        fields->m_currentFrameIndex = 0;
        fields->m_ghostSprite = nullptr;
        fields->m_hasGhost = false;
        fields->m_isRecording = false;
        fields->m_saveRequested = false;

        // Load ghost file if it exists
        int levelID = level ? level->m_levelID : -1;
        auto saveDir = Mod::get()->getModSettingSaveDir();
        std::filesystem::create_directories(saveDir);

        if (levelID >= 0) {
            auto savePath = saveDir / (std::to_string(levelID) + ".json");

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
                            fields->m_playbackFrames.push_back(frame);
                        }
                        fields->m_hasGhost = !fields->m_playbackFrames.empty();
                    }
                }
            }
        }

        // Setup the physical ghost sprite if playback data exists
        if (fields->m_hasGhost) {
            fields->m_ghostSprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            if (fields->m_ghostSprite) {
                int r = Mod::get()->getSavedValue<int>("color_r_" + std::to_string(levelID), 0);
                int g = Mod::get()->getSavedValue<int>("color_g_" + std::to_string(levelID), 255);
                int b = Mod::get()->getSavedValue<int>("color_b_" + std::to_string(levelID), 255);

                fields->m_ghostSprite->setColor({static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b)});
                fields->m_ghostSprite->setOpacity(150);
                m_objectLayer->addChild(fields->m_ghostSprite);
            }
        }

        return true;
    }

    // 2. Continuous frame recorder & playback animator
    void update(float dt) {
        PlayLayer::update(dt);

        auto fields = m_fields;

        // Recording is enabled while practice mode is active.
        // If you later add a "Record New Route" button, set fields->m_isRecording = true there.
        fields->m_isRecording = this->m_isPracticeMode;

        // Record current frame
        if (fields->m_isRecording && this->m_player1) {
            GhostFrame currentFrame = {
                this->m_player1->m_position.x,
                this->m_player1->m_position.y,
                this->m_player1->getRotation()
            };
            fields->m_recordedFrames.push_back(currentFrame);
        }

        // Playback system
        if (fields->m_hasGhost && fields->m_ghostSprite) {
            if (fields->m_currentFrameIndex < fields->m_playbackFrames.size()) {
                auto& frame = fields->m_playbackFrames[fields->m_currentFrameIndex];
                fields->m_ghostSprite->setPosition({frame.x, frame.y});
                fields->m_ghostSprite->setRotation(frame.rotation);
                fields->m_currentFrameIndex++;
            } else {
                fields->m_ghostSprite->setVisible(false);
            }
        }
    }

    void saveGhostToDisk() {
        auto fields = m_fields;

        if (!fields->m_isRecording || fields->m_saveRequested || fields->m_recordedFrames.empty()) {
            return;
        }

        int levelID = this->m_level ? this->m_level->m_levelID : -1;
        if (levelID < 0) {
            return;
        }

        fields->m_saveRequested = true;

        auto saveDir = Mod::get()->getModSettingSaveDir();
        std::filesystem::create_directories(saveDir);
        auto savePath = saveDir / (std::to_string(levelID) + ".json");

        matjson::Value jsonOutput = matjson::Object();
        matjson::Value jsonFrames = matjson::Array();

        for (const auto& frame : fields->m_recordedFrames) {
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
};

// Continuing the MyPlayLayer implementation...
class $modify(MyPlayLayerEngine, PlayLayer) {
    // 3. Keep track of vector size when a checkpoint is placed
    void createCheckpoint() {
        PlayLayer::createCheckpoint();
        auto fields = reinterpret_cast<MyPlayLayer*>(this)->m_fields.value();
        fields->m_checkpointFrameCounts.push_back(fields->m_recordedFrames.size());
    }

    // 4. Handle manual deletion of checkpoints
    void removeLastCheckpoint() {
        PlayLayer::removeLastCheckpoint();
        auto fields = reinterpret_cast<MyPlayLayer*>(this)->m_fields.value();
        if (!fields->m_checkpointFrameCounts.empty()) {
            fields->m_checkpointFrameCounts.pop_back();
        }
    }

    // 5. Rewind recorded frames to the last valid checkpoint on death
    void loadFromCheckpoint() {
        PlayLayer::loadFromCheckpoint();
        auto fields = reinterpret_cast<MyPlayLayer*>(this)->m_fields.value();

        fields->m_currentFrameIndex = 0;
        if (fields->m_ghostSprite) {
            fields->m_ghostSprite->setVisible(true);
        }

        if (!fields->m_checkpointFrameCounts.empty()) {
            size_t framesAtCheckpoint = fields->m_checkpointFrameCounts.back();
            if (framesAtCheckpoint < fields->m_recordedFrames.size()) {
                fields->m_recordedFrames.erase(
                    fields->m_recordedFrames.begin() + framesAtCheckpoint,
                    fields->m_recordedFrames.end()
                );
            }
        } else {
            fields->m_recordedFrames.clear();
        }
    }

    // 6. Save once when level is complete
    void levelComplete() {
        PlayLayer::levelComplete();
        reinterpret_cast<MyPlayLayer*>(this)->saveGhostToDisk();
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
        std::filesystem::create_directories(dir);

        float yOffset = winSize.height / 2 + h / 2 - 50;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();

                auto nameLbl = CCLabelBMFont::create(filename.c_str(), "goldFont.fnt");
                nameLbl->setPosition({winSize.width / 2, yOffset});
                nameLbl->setScale(0.5f);
                this->addChild(nameLbl);

                auto delSprite = CCSprite::createWithSpriteFrameName("edit_delBtnSmall_001.png");
                auto delBtn = CCMenuItemSpriteExtra::create(delSprite, this, menu_selector(GhostManagerPopup::onDeleteGhost));
                delBtn->setPosition({winSize.width / 2 - w / 2 + 30, yOffset});
                delBtn->setID(filename);
                itemMenu->addChild(delBtn);

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
        this->setupList(240, 200);
    }

    void onCycleColor(CCObject* sender) {
        auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
        std::string levelID = btn->getID();

        int curColor = Mod::get()->getSavedValue<int>("color_state_" + levelID, 0);
        curColor = (curColor + 1) % 4;
        Mod::get()->setSavedValue<int>("color_state_" + levelID, curColor);

        int r = 0, g = 255, b = 255;
        if (curColor == 1) { r = 255; g = 0; b = 0; }
        else if (curColor == 2) { r = 0; g = 255; b = 0; }
        else if (curColor == 3) { r = 255; g = 255; b = 0; }

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
            auto ghostSprite = CCSprite::createWithSpriteFrameName("GJ_profileButton_001.png");
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
