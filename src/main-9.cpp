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
#include <functional>

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
    bool m_firstFramePinged = false;  // debug-only: confirms recordFrame() is actually reached
    bool m_updateDiagnosticPinged = false; // debug-only: separate guard so this never fires more than once
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

        if (!m_firstFramePinged) {
            m_firstFramePinged = true;
            GhostDebug::popup("Ghost Debug", "recordFrame() confirmed working - first frame captured");
        }
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

        matjson::Value root = matjson::Value::array();
        for (auto& ghost : m_loadedGhosts) {
            matjson::Value entry = matjson::Value::object();
            entry["routeName"] = ghost.routeName;
            entry["filename"] = ghost.filename;
            entry["color_r"] = (int)ghost.color.r;
            entry["color_g"] = (int)ghost.color.g;
            entry["color_b"] = (int)ghost.color.b;
            entry["enabled"] = ghost.enabled;
            entry["lookAheadTicks"] = ghost.lookAheadTicks;
            root.push(entry);
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

        // matjson::parse returns geode::Result<Value, ParseError>, not std::optional -
        // check truthiness directly and use .unwrap(), not .has_value()/.value().
        auto parsed = matjson::parse(buffer.str());
        if (!parsed) {
            GhostDebug::popup("Ghost Debug ERROR", "Failed to parse metadata.json");
            return;
        }
        matjson::Value root = parsed.unwrap();

        // Value supports range-based for directly for arrays - no .as_array() needed.
        for (auto& entry : root) {
            RuntimeGhost ghost;
            ghost.routeName = entry["routeName"].asString().unwrapOr("Unnamed Route");
            ghost.filename = entry["filename"].asString().unwrapOr("");
            ghost.color = {
                static_cast<GLubyte>(entry["color_r"].asInt().unwrapOr(255)),
                static_cast<GLubyte>(entry["color_g"].asInt().unwrapOr(255)),
                static_cast<GLubyte>(entry["color_b"].asInt().unwrapOr(255))
            };
            ghost.enabled = entry["enabled"].asBool().unwrapOr(true);
            ghost.lookAheadTicks = entry["lookAheadTicks"].asInt().unwrapOr(0);

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
        if (!parsed) {
            GhostDebug::popup("Ghost Debug ERROR", "Bad frame data in: " + ghost.filename);
            return;
        }
        matjson::Value root = parsed.unwrap();
        if (!root.contains("frames")) {
            GhostDebug::popup("Ghost Debug ERROR", "No 'frames' key in: " + ghost.filename);
            return;
        }

        for (auto& f : root["frames"]) {
            GhostFrame frame;
            frame.tick = f["tick"].asInt().unwrapOr(0);
            frame.x = f["x"].asDouble().unwrapOr(0.0);
            frame.y = f["y"].asDouble().unwrapOr(0.0);
            frame.rotation = f["rot"].asDouble().unwrapOr(0.0);
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

        matjson::Value root = matjson::Value::object();
        matjson::Value framesArr = matjson::Value::array();
        for (auto& f : m_recordingBuffer) {
            matjson::Value entry = matjson::Value::object();
            entry["tick"] = f.tick;
            entry["x"] = f.x;
            entry["y"] = f.y;
            entry["rot"] = f.rotation;
            framesArr.push(entry);
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
class GhostPlayLayer; // fwd declare, hooked class below

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
            auto nameLbl = CCLabelBMFont::create(ghost.routeName.c_str(), "goldFont.fnt");
            nameLbl->setScale(0.45f);
            nameLbl->setAnchorPoint({0, 0.5f});
            m_listLayer->addChildAtPosition(nameLbl, Anchor::Center, {-95, yOffset});

            auto toggleSprite = ButtonSprite::create(ghost.enabled ? "On" : "Off");
            auto toggleBtn = CCMenuItemSpriteExtra::create(toggleSprite, this, menu_selector(GhostPopup::onToggle));
            toggleBtn->setPosition({-10, yOffset});
            toggleBtn->setID(ghost.filename);
            itemMenu->addChild(toggleBtn);

            auto renameSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
            renameSprite->setScale(0.5f);
            auto renameBtn = CCMenuItemSpriteExtra::create(renameSprite, this, menu_selector(GhostPopup::onRename));
            renameBtn->setPosition({30, yOffset});
            renameBtn->setID(ghost.filename);
            itemMenu->addChild(renameBtn);

            auto colorSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
            colorSprite->setScale(0.5f);
            auto colorBtn = CCMenuItemSpriteExtra::create(colorSprite, this, menu_selector(GhostPopup::onCycleColor));
            colorBtn->setPosition({65, yOffset});
            colorBtn->setID(ghost.filename);
            itemMenu->addChild(colorBtn);

            auto delSprite = CCSprite::createWithSpriteFrameName("edit_delBtnSmall_001.png");
            auto delBtn = CCMenuItemSpriteExtra::create(delSprite, this, menu_selector(GhostPopup::onDelete));
            delBtn->setPosition({100, yOffset});
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

// =====================================================================
// GHOST PLAY LAYER
// Single merged $modify(PlayLayer) hook - owns the GhostManager instance
// for this play session via Fields.
// =====================================================================
class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        GhostManager m_ghostManager;
        bool m_updateEverPinged = false; // debug-only: proves update() is being called at all, regardless of recording state
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) override {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        m_fields->m_ghostManager.m_levelID = level ? level->m_levelID : -1;
        m_fields->m_ghostManager.loadMetadataAndGhosts();
        m_fields->m_ghostManager.spawnGhostSprites(m_objectLayer);

        return true;
    }

    void update(float dt) override {
        PlayLayer::update(dt);

        if (!m_fields->m_updateEverPinged) {
            m_fields->m_updateEverPinged = true;
            GhostDebug::popup("Ghost Debug", "update() hook IS running (unconditional check)");
        }

        auto& manager = m_fields->m_ghostManager;

        if (manager.m_isRecording && !manager.m_updateDiagnosticPinged) {
            manager.m_updateDiagnosticPinged = true;
            // One-time diagnostic: confirms whether m_player1 exists at all
            // while recording is supposedly active.
            GhostDebug::popup("Ghost Debug",
                std::string("update() reached, isRecording=true, m_player1 is ") +
                (this->m_player1 ? "VALID" : "NULL"));
        }

        if (manager.m_isRecording && this->m_player1) {
            manager.recordFrame(
                this->m_player1->m_position.x,
                this->m_player1->m_position.y,
                this->m_player1->getRotation()
            );
        }

        manager.tickPlayback();
    }

    CheckpointObject* createCheckpoint() override {
        auto result = PlayLayer::createCheckpoint();
        m_fields->m_ghostManager.onCheckpointCreated();
        return result;
    }

    void removeCheckpoint(bool first) override {
        PlayLayer::removeCheckpoint(first);
        m_fields->m_ghostManager.onCheckpointRemoved();
    }

    void loadFromCheckpoint(CheckpointObject* object) override {
        PlayLayer::loadFromCheckpoint(object);
        m_fields->m_ghostManager.rewindToLastCheckpoint();

        for (auto& ghost : m_fields->m_ghostManager.m_loadedGhosts) {
            ghost.playbackIndex = 0;
            if (ghost.sprite) ghost.sprite->setVisible(true);
        }
    }

    void levelComplete() override {
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
