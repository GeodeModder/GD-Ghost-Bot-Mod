#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

using namespace geode::prelude;

// =====================================================================
// DEBUG HELPER
// Fire a popup anywhere we want in-game visibility. This is what
// catches silent failures (like "0 frames recorded") immediately
// instead of discovering them after a full playthrough.
// Flip DEBUG_MODE to false once things are verified stable.
// =====================================================================
namespace GhostDebug {
    constexpr bool DEBUG_MODE = true;

    inline void popup(const std::string& title, const std::string& message) {
        if (!DEBUG_MODE) return;
        FLAlertLayer::create(title.c_str(), message.c_str(), "OK")->show();
    }

    inline void frameCount(const std::string& context, size_t count) {
        popup("Ghost Debug", context + ": " + std::to_string(count) + " frames");
    }
}

// =====================================================================
// DATA STRUCTURES
// =====================================================================

struct GhostFrame {
    int tick;
    float x;
    float y;
    float rotation;
};

struct RuntimeGhost {
    std::string routeName;
    std::string filename;
    cocos2d::ccColor3B color = {255, 255, 255};
    bool enabled = true;
    int lookAheadTicks = 0;

    std::vector<GhostFrame> frames;

    // Playback-only, not saved to disk
    size_t playbackIndex = 0;
    SimplePlayer* sprite = nullptr;
};

// =====================================================================
// GHOST MANAGER
// Owned per-PlayLayer-session as a field on GhostPlayLayer.
// =====================================================================
class GhostManager {
public:
    std::vector<RuntimeGhost> m_loadedGhosts;
    std::vector<GhostFrame> m_recordingBuffer;
    std::vector<size_t> m_checkpointFrameCounts;

    bool m_isRecording = false;
    bool m_saveFlowTriggered = false; // guards against double-save on levelComplete
    int m_currentTick = 0;
    int m_levelID = -1;

    std::filesystem::path saveDirForLevel() {
        auto base = Mod::get()->getSaveDir() / "ghosts" / std::to_string(m_levelID);
        std::filesystem::create_directories(base);
        return base;
    }

    // ---------------- Recording ----------------

    void startRecording() {
        m_recordingBuffer.clear();
        m_checkpointFrameCounts.clear();
        m_currentTick = 0;
        m_isRecording = true;
        m_saveFlowTriggered = false;
        GhostDebug::popup("Ghost Debug", "Recording started");
    }

    void stopRecording() {
        m_isRecording = false;
        GhostDebug::frameCount("Recording stopped", m_recordingBuffer.size());
    }

    void recordFrame(float x, float y, float rotation) {
        if (!m_isRecording) return;
        m_recordingBuffer.push_back({m_currentTick, x, y, rotation});
        m_currentTick++;
    }

    void onCheckpointCreated() {
        if (!m_isRecording) return;
        m_checkpointFrameCounts.push_back(m_recordingBuffer.size());
    }

    void onCheckpointRemoved() {
        if (!m_checkpointFrameCounts.empty()) {
            m_checkpointFrameCounts.pop_back();
        }
    }

    void rewindToLastCheckpoint() {
        if (!m_isRecording) return;
        size_t before = m_recordingBuffer.size();

        if (!m_checkpointFrameCounts.empty()) {
            size_t target = m_checkpointFrameCounts.back();
            if (target < m_recordingBuffer.size()) {
                m_recordingBuffer.erase(
                    m_recordingBuffer.begin() + target,
                    m_recordingBuffer.end()
                );
            }
            m_currentTick = static_cast<int>(m_recordingBuffer.size());
        } else {
            m_recordingBuffer.clear();
            m_currentTick = 0;
        }

        GhostDebug::popup("Ghost Debug",
            "Rewound from " + std::to_string(before) +
            " to " + std::to_string(m_recordingBuffer.size()) + " frames");
    }

    // ---------------- Save / Load ----------------

    // Metadata for all routes on this level: name, color, enabled, filename, lookahead.
    void saveMetadata() {
        auto path = saveDirForLevel() / "metadata.json";

        matjson::Value root = matjson::Array();
        for (auto& ghost : m_loadedGhosts) {
            matjson::Value entry = matjson::Object();
            entry["routeName"] = ghost.routeName;
            entry["filename"] = ghost.filename;
            entry["color_r"] = (int)ghost.color.r;
            entry["color_g"] = (int)ghost.color.g;
            entry["color_b"] = (int)ghost.color.b;
            entry["enabled"] = ghost.enabled;
            entry["lookAheadTicks"] = ghost.lookAheadTicks;
            root.push_back(entry);
        }

        std::ofstream file(path);
        if (file.is_open()) {
            file << root.dump();
            file.close();
            GhostDebug::popup("Ghost Debug", "Metadata saved (" + std::to_string(m_loadedGhosts.size()) + " routes)");
        } else {
            GhostDebug::popup("Ghost Debug ERROR", "Failed to open metadata.json for writing");
        }
    }

    void loadMetadataAndGhosts() {
        m_loadedGhosts.clear();
        auto path = saveDirForLevel() / "metadata.json";

        if (!std::filesystem::exists(path)) {
            GhostDebug::popup("Ghost Debug", "No metadata.json found for this level yet");
            return;
        }

        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        auto parsed = matjson::parse(buffer.str());
        if (!parsed.has_value()) {
            GhostDebug::popup("Ghost Debug ERROR", "Failed to parse metadata.json");
            return;
        }

        for (auto& entry : parsed.value().as_array()) {
            RuntimeGhost ghost;
            ghost.routeName = entry["routeName"].as_string();
            ghost.filename = entry["filename"].as_string();
            ghost.color = {
                static_cast<GLubyte>(entry["color_r"].as_int()),
                static_cast<GLubyte>(entry["color_g"].as_int()),
                static_cast<GLubyte>(entry["color_b"].as_int())
            };
            ghost.enabled = entry["enabled"].as_bool();
            ghost.lookAheadTicks = entry["lookAheadTicks"].as_int();

            loadFramesForGhost(ghost);
            m_loadedGhosts.push_back(ghost);
        }

        GhostDebug::popup("Ghost Debug", "Loaded " + std::to_string(m_loadedGhosts.size()) + " routes");
    }

    void loadFramesForGhost(RuntimeGhost& ghost) {
        auto path = saveDirForLevel() / ghost.filename;
        if (!std::filesystem::exists(path)) {
            GhostDebug::popup("Ghost Debug ERROR", "Missing route file: " + ghost.filename);
            return;
        }

        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        auto parsed = matjson::parse(buffer.str());
        if (!parsed.has_value() || !parsed.value().contains("frames")) {
            GhostDebug::popup("Ghost Debug ERROR", "Bad frame data in: " + ghost.filename);
            return;
        }

        for (auto& f : parsed.value()["frames"].as_array()) {
            GhostFrame frame;
            frame.tick = f["tick"].as_int();
            frame.x = f["x"].as_double();
            frame.y = f["y"].as_double();
            frame.rotation = f["rot"].as_double();
            ghost.frames.push_back(frame);
        }
    }

    // Saves the current recording buffer as a brand new named route.
    void saveRecordingAsRoute(const std::string& routeName) {
        if (m_saveFlowTriggered) {
            GhostDebug::popup("Ghost Debug", "Save already triggered, skipping duplicate");
            return;
        }
        m_saveFlowTriggered = true;

        GhostDebug::frameCount("Saving route '" + routeName + "'", m_recordingBuffer.size());

        if (m_recordingBuffer.empty()) {
            GhostDebug::popup("Ghost Debug ERROR", "Recording buffer is EMPTY - nothing to save. Check that recordFrame() is actually being called in update().");
            return;
        }

        std::string filename = routeName + "_" + std::to_string(std::rand()) + ".json";

        matjson::Value root = matjson::Object();
        matjson::Value framesArr = matjson::Array();
        for (auto& f : m_recordingBuffer) {
            matjson::Value entry = matjson::Object();
            entry["tick"] = f.tick;
            entry["x"] = f.x;
            entry["y"] = f.y;
            entry["rot"] = f.rotation;
            framesArr.push_back(entry);
        }
        root["frames"] = framesArr;

        auto path = saveDirForLevel() / filename;
        std::ofstream file(path);
        if (file.is_open()) {
            file << root.dump();
            file.close();
        } else {
            GhostDebug::popup("Ghost Debug ERROR", "Failed to write route file: " + filename);
            return;
        }

        RuntimeGhost newGhost;
        newGhost.routeName = routeName;
        newGhost.filename = filename;
        newGhost.color = {255, 255, 255};
        newGhost.enabled = true;
        newGhost.lookAheadTicks = 0;
        newGhost.frames = m_recordingBuffer;
        m_loadedGhosts.push_back(newGhost);

        saveMetadata();
        GhostDebug::popup("Ghost Debug", "Route '" + routeName + "' saved successfully");
    }

    void deleteGhost(const std::string& filename) {
        auto path = saveDirForLevel() / filename;
        std::filesystem::remove(path);

        m_loadedGhosts.erase(
            std::remove_if(m_loadedGhosts.begin(), m_loadedGhosts.end(),
                [&](const RuntimeGhost& g) {
                    if (g.filename == filename && g.sprite) {
                        g.sprite->removeFromParentAndCleanup(true);
                    }
                    return g.filename == filename;
                }),
            m_loadedGhosts.end()
        );

        saveMetadata();
    }

    void renameGhost(const std::string& filename, const std::string& newName) {
        for (auto& g : m_loadedGhosts) {
            if (g.filename == filename) {
                g.routeName = newName;
            }
        }
        saveMetadata();
    }

    // ---------------- Playback ----------------

    void spawnGhostSprites(cocos2d::CCLayer* parentLayer) {
        for (auto& ghost : m_loadedGhosts) {
            if (!ghost.enabled) continue;

            ghost.sprite = SimplePlayer::create(1);
            if (ghost.sprite) {
                ghost.sprite->setColor(ghost.color);
                ghost.sprite->setOpacity(150);
                ghost.playbackIndex = 0;
                parentLayer->addChild(ghost.sprite);
            } else {
                GhostDebug::popup("Ghost Debug ERROR", "Failed to create SimplePlayer for " + ghost.routeName);
            }
        }
    }

    void tickPlayback() {
        for (auto& ghost : m_loadedGhosts) {
            if (!ghost.enabled || !ghost.sprite) continue;

            size_t idx = ghost.playbackIndex + static_cast<size_t>(std::max(0, ghost.lookAheadTicks));

            if (idx < ghost.frames.size()) {
                auto& frame = ghost.frames[idx];
                ghost.sprite->setPosition({frame.x, frame.y});
                ghost.sprite->setRotation(frame.rotation);
                ghost.playbackIndex++;
            } else {
                ghost.sprite->setVisible(false);
            }
        }
    }
};

// =====================================================================
// GHOST NAME DIALOG
// Used both for naming a brand-new route and renaming an existing one.
// =====================================================================
class GhostNameDialog : public FLAlertLayer, public TextInputDelegate {
public:
    std::function<void(const std::string&)> m_onConfirm;
    CCTextInputNode* m_input = nullptr;
    bool m_resultHandled = false; // guards against double-callback / stuck popup

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

    bool init(const std::string& defaultText) {
        if (!FLAlertLayer::init()) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({240, 100});
        bg->setPosition(winSize / 2);
        this->addChild(bg);

        m_input = CCTextInputNode::create(200, 30, "Route name", "chatFont.fnt");
        m_input->setPosition({winSize.width / 2, winSize.height / 2 + 10});
        m_input->setString(defaultText);
        m_input->setDelegate(this);
        this->addChild(m_input);

        auto confirmSprite = ButtonSprite::create("Save");
        auto confirmBtn = CCMenuItemSpriteExtra::create(confirmSprite, this, menu_selector(GhostNameDialog::onConfirm));
        confirmBtn->setPosition({winSize.width / 2 + 50, winSize.height / 2 - 25});

        auto cancelSprite = ButtonSprite::create("Cancel");
        auto cancelBtn = CCMenuItemSpriteExtra::create(cancelSprite, this, menu_selector(GhostNameDialog::onCancel));
        cancelBtn->setPosition({winSize.width / 2 - 50, winSize.height / 2 - 25});

        auto menu = CCMenu::create(confirmBtn, cancelBtn, nullptr);
        menu->setPosition({0, 0});
        this->addChild(menu);

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);

        return true;
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
class GhostPlayLayer; // fwd declare, hooked class below

class GhostPopup : public FLAlertLayer {
public:
    GhostManager* m_manager = nullptr;
    cocos2d::CCLayer* m_listLayer = nullptr;

    static GhostPopup* create(GhostManager* manager) {
        auto ret = new GhostPopup();
        if (ret && ret->init(manager)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init(GhostManager* manager) {
        if (!FLAlertLayer::init()) return false;
        m_manager = manager;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({280, 220});
        bg->setPosition(winSize / 2);
        this->addChild(bg);

        auto title = CCLabelBMFont::create("Ghost Manager", "bigFont.fnt");
        title->setPosition({winSize.width / 2, winSize.height / 2 + 95});
        title->setScale(0.6f);
        this->addChild(title);

        auto recordSprite = ButtonSprite::create("Record New Route");
        auto recordBtn = CCMenuItemSpriteExtra::create(recordSprite, this, menu_selector(GhostPopup::onRecordNew));
        recordBtn->setPosition({winSize.width / 2, winSize.height / 2 + 65});

        auto closeSprite = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        auto closeBtn = CCMenuItemSpriteExtra::create(closeSprite, this, menu_selector(GhostPopup::onClose));
        closeBtn->setPosition({winSize.width / 2 + 130, winSize.height / 2 + 100});

        auto topMenu = CCMenu::create(recordBtn, closeBtn, nullptr);
        topMenu->setPosition({0, 0});
        this->addChild(topMenu);

        m_listLayer = CCLayer::create();
        this->addChild(m_listLayer);

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);

        refreshList();
        return true;
    }

    void refreshList() {
        m_listLayer->removeAllChildrenWithCleanup(true);
        if (!m_manager) return;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto itemMenu = CCMenu::create();
        itemMenu->setPosition({0, 0});

        float yOffset = winSize.height / 2 + 35;

        for (auto& ghost : m_manager->m_loadedGhosts) {
            auto nameLbl = CCLabelBMFont::create(ghost.routeName.c_str(), "goldFont.fnt");
            nameLbl->setPosition({winSize.width / 2 - 40, yOffset});
            nameLbl->setScale(0.45f);
            nameLbl->setAnchorPoint({0, 0.5f});
            m_listLayer->addChild(nameLbl);

            auto toggleSprite = ButtonSprite::create(ghost.enabled ? "On" : "Off");
            auto toggleBtn = CCMenuItemSpriteExtra::create(toggleSprite, this, menu_selector(GhostPopup::onToggle));
            toggleBtn->setPosition({winSize.width / 2 + 20, yOffset});
            toggleBtn->setID(ghost.filename);
            itemMenu->addChild(toggleBtn);

            auto renameSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
            renameSprite->setScale(0.5f);
            auto renameBtn = CCMenuItemSpriteExtra::create(renameSprite, this, menu_selector(GhostPopup::onRename));
            renameBtn->setPosition({winSize.width / 2 + 55, yOffset});
            renameBtn->setID(ghost.filename);
            itemMenu->addChild(renameBtn);

            auto colorSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
            colorSprite->setScale(0.5f);
            auto colorBtn = CCMenuItemSpriteExtra::create(colorSprite, this, menu_selector(GhostPopup::onCycleColor));
            colorBtn->setPosition({winSize.width / 2 + 85, yOffset});
            colorBtn->setID(ghost.filename);
            itemMenu->addChild(colorBtn);

            auto delSprite = CCSprite::createWithSpriteFrameName("edit_delBtnSmall_001.png");
            auto delBtn = CCMenuItemSpriteExtra::create(delSprite, this, menu_selector(GhostPopup::onDelete));
            delBtn->setPosition({winSize.width / 2 + 115, yOffset});
            delBtn->setID(ghost.filename);
            itemMenu->addChild(delBtn);

            yOffset -= 30;
        }

        m_listLayer->addChild(itemMenu);
    }

    void onRecordNew(CCObject*) {
        if (!m_manager) return;
        m_manager->startRecording();
        this->removeFromParentAndCleanup(true);
        GhostDebug::popup("Ghost Debug", "Recording will begin now - complete or reset the level to finish");
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

// =====================================================================
// GHOST PLAY LAYER
// Single merged $modify(PlayLayer) hook - owns the GhostManager instance
// for this play session via Fields.
// =====================================================================
class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        GhostManager m_ghostManager;
    };

    bool init(GJGameLevel* level) {
        if (!PlayLayer::init(level)) return false;

        m_fields->m_ghostManager.m_levelID = level ? level->m_levelID : -1;
        m_fields->m_ghostManager.loadMetadataAndGhosts();
        m_fields->m_ghostManager.spawnGhostSprites(m_objectLayer);

        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        auto& manager = m_fields->m_ghostManager;

        if (manager.m_isRecording && this->m_player1) {
            manager.recordFrame(
                this->m_player1->m_position.x,
                this->m_player1->m_position.y,
                this->m_player1->getRotation()
            );
        }

        manager.tickPlayback();
    }

    CheckpointObject* createCheckpoint() {
        auto result = PlayLayer::createCheckpoint();
        m_fields->m_ghostManager.onCheckpointCreated();
        return result;
    }

    void removeCheckpoint(bool first) {
        PlayLayer::removeCheckpoint(first);
        m_fields->m_ghostManager.onCheckpointRemoved();
    }

    void loadFromCheckpoint(CheckpointObject* object) {
        PlayLayer::loadFromCheckpoint(object);
        m_fields->m_ghostManager.rewindToLastCheckpoint();

        for (auto& ghost : m_fields->m_ghostManager.m_loadedGhosts) {
            ghost.playbackIndex = 0;
            if (ghost.sprite) ghost.sprite->setVisible(true);
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();

        auto& manager = m_fields->m_ghostManager;
        if (!manager.m_isRecording) return;

        manager.stopRecording();

        // Give the completion animation a moment before showing the save dialog
        this->runAction(CCSequence::create(
            CCDelayTime::create(1.5f),
            CCCallFunc::create(this, callfunc_selector(GhostPlayLayer::promptSaveDialog)),
            nullptr
        ));
    }

    void promptSaveDialog() {
        auto manager = &m_fields->m_ghostManager;
        auto dialog = GhostNameDialog::create("My Route", [manager](const std::string& name) {
            manager->saveRecordingAsRoute(name);
        });
        dialog->show();
    }
};

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
        auto playLayer = static_cast<GhostPlayLayer*>(PlayLayer::get());
        if (!playLayer) {
            GhostDebug::popup("Ghost Debug ERROR", "Could not get current PlayLayer");
            return;
        }

        auto popup = GhostPopup::create(&playLayer->m_fields->m_ghostManager);
        popup->show();
    }
};
