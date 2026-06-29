#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/ui/TextInput.hpp>   
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <chrono> 

using namespace geode::prelude;

class GhostPopup;

struct GhostFrame {
    uint32_t tick; 
    float x, y, rot;
};

struct RuntimeGhost {
    std::string name;
    ccColor3B color;
    bool isEnabled;
    std::string filename; 
    uint32_t lookAheadTicks = 0; 
    std::vector<GhostFrame> frames;
};

class GhostManager {
private:
    GhostManager() {}
    std::vector<RuntimeGhost> m_activeGhosts;
    bool m_isRecording = false;
    std::string m_recordingTargetName = "";
    std::vector<GhostFrame> m_recordingBuffer;

public:
    static GhostManager* get() {
        static GhostManager instance;
        return &instance;
    }

    std::vector<RuntimeGhost>& getActiveGhosts() { return m_activeGhosts; }
    bool isRecording() const { return m_isRecording; }
    void setRecording(bool state) { m_isRecording = state; }
    std::vector<GhostFrame>& getRecordingBuffer() { return m_recordingBuffer; }
    std::string getRecordingName() const { return m_recordingTargetName; }
    void setRecordingName(std::string const& name) { m_recordingTargetName = name; }

    void clearVolatileBuffers() {
        m_isRecording = false;
        m_recordingBuffer.clear();
        m_recordingTargetName = "";
    }

    std::string getUniqueRouteName(std::string const& baseName, std::string const& ignoreFilename = "") {
        std::string uniqueName = baseName;
        int counter = 2;
        bool exists = true;
        while (exists) {
            exists = false;
            for (auto const& ghost : m_activeGhosts) {
                if (!ignoreFilename.empty() && ghost.filename == ignoreFilename) continue;
                if (ghost.name == uniqueName) {
                    uniqueName = baseName + " (" + std::to_string(counter) + ")";
                    counter++;
                    exists = true;
                    break;
                }
            }
        }
        return uniqueName;
    }

    void saveMetadataFile(int levelID) {
        auto dir = Mod::get()->getSaveDir() / std::to_string(levelID);
        std::filesystem::create_directories(dir);
        auto metaPath = dir / "metadata.json";

        auto ghostsArr = matjson::Value::array();
        for (auto const& g : m_activeGhosts) {
            auto ghostObj = matjson::Value::object();
            ghostObj["name"] = g.name;
            ghostObj["enabled"] = g.isEnabled;
            ghostObj["filename"] = g.filename;
            ghostObj["lookahead_ticks"] = static_cast<int>(g.lookAheadTicks);

            auto colArr = matjson::Value::array();
            colArr.push(static_cast<int>(g.color.r));
            colArr.push(static_cast<int>(g.color.g));
            colArr.push(static_cast<int>(g.color.b));
            ghostObj["color"] = colArr;

            ghostsArr.push(ghostObj);
        }

        auto root = matjson::Value::object();
        root["ghosts"] = ghostsArr;

        std::ofstream file(metaPath);
        if (!file.fail()) {
            file << root.dump(4);
        }
        file.close();
    }

    void loadMacroFramework(int levelID) {
        m_activeGhosts.clear();
        auto dir = Mod::get()->getSaveDir() / std::to_string(levelID);
        auto metaPath = dir / "metadata.json";

        if (!std::filesystem::exists(metaPath)) return;

        std::ifstream metaFile(metaPath);
        if (metaFile.fail()) return;

        std::string metaStr((std::istreambuf_iterator<char>(metaFile)), std::istreambuf_iterator<char>());
        metaFile.close();

        auto metaJsonResult = matjson::parse(metaStr);
        if (!metaJsonResult) return;
        auto metaJson = metaJsonResult.unwrap();

        if (!metaJson.contains("ghosts") || !metaJson["ghosts"].isArray()) return;

        for (auto const& g : metaJson["ghosts"]) {
            if (!g.contains("name") || !g.contains("filename")) continue;

            RuntimeGhost ghost;
            ghost.name = g["name"].asString().unwrapOrDefault();
            ghost.isEnabled = g.contains("enabled") ? g["enabled"].asBool().unwrapOrDefault() : true;
            ghost.filename = g["filename"].asString().unwrapOrDefault();
            ghost.lookAheadTicks = g.contains("lookahead_ticks") ? static_cast<uint32_t>(g["lookahead_ticks"].asInt().unwrapOrDefault()) : 0;

            if (g.contains("color") && g["color"].isArray() && g["color"].size() >= 3) {
                ghost.color.r = static_cast<uint8_t>(g["color"][0].asInt().unwrapOrDefault());
                ghost.color.g = static_cast<uint8_t>(g["color"][1].asInt().unwrapOrDefault());
                ghost.color.b = static_cast<uint8_t>(g["color"][2].asInt().unwrapOrDefault());
            } else {
                ghost.color = {0, 255, 255}; 
            }

            auto macroPath = dir / ghost.filename;
            if (std::filesystem::exists(macroPath)) {
                std::ifstream macroFile(macroPath);
                if (!macroFile.fail()) {
                    std::string macroStr((std::istreambuf_iterator<char>(macroFile)), std::istreambuf_iterator<char>());
                    macroFile.close();

                    auto macroJsonResult = matjson::parse(macroStr);
                    if (macroJsonResult) {
                        auto macroJson = macroJsonResult.unwrap();
                        if (macroJson.contains("frames") && macroJson["frames"].isArray()) {
                            for (auto const& f : macroJson["frames"]) {
                                if (!f.contains("tick") || !f.contains("x") || !f.contains("y") || !f.contains("rot")) continue;
                                if (!f["tick"].isNumber() || !f["x"].isNumber() || !f["y"].isNumber() || !f["rot"].isNumber()) continue;

                                ghost.frames.push_back({
                                    static_cast<uint32_t>(f["tick"].asInt().unwrapOrDefault()),
                                    static_cast<float>(f["x"].asDouble().unwrapOrDefault()),
                                    static_cast<float>(f["y"].asDouble().unwrapOrDefault()),
                                    static_cast<float>(f["rot"].asDouble().unwrapOrDefault())
                                });
                            }
                        }
                    }
                }
            }
            m_activeGhosts.push_back(ghost);
        }
    }
};

// ---------------------------------------------------------------------------
// GhostNameDialog
// Fix (Bug 2): The original code passed `this` as the FLAlertLayerProtocol
// delegate, making the layer retain itself and preventing deallocation after
// dismissal (zombie node that intercepted touches). Fix: we still pass `this`
// so FLAlertLayer can call FLAlert_Clicked (needed to drive its own button
// logic), but we null out m_alertProtocol at the very start of FLAlert_Clicked
// to break the cycle before the callback returns and FLAlertLayer dismisses.
// ---------------------------------------------------------------------------
class GhostNameDialog : public FLAlertLayer, public FLAlertLayerProtocol {
protected:
    int m_levelID;
    bool m_isRenameMode;
    size_t m_editIndex;
    geode::TextInput* m_inputField = nullptr;
    GhostPopup* m_parentPopup = nullptr;

    bool init(int levelID, bool isRenameMode, size_t editIndex, GhostPopup* parentPopup);

    // setTouchPriority() called in init() is too early — FLAlertLayer registers
    // its touch handler in onEnter(), which overwrites any priority set before.
    // Override onEnter() to force priority AFTER base registration.
    void onEnter() override {
        FLAlertLayer::onEnter();
        this->setTouchPriority(-500);
        if (m_buttonMenu) m_buttonMenu->setTouchPriority(-501);
    }

public:
    static GhostNameDialog* create(int levelID, bool isRenameMode, size_t editIndex = 0, GhostPopup* parentPopup = nullptr) {
        auto ret = new GhostNameDialog();
        if (ret && ret->init(levelID, isRenameMode, editIndex, parentPopup)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void FLAlert_Clicked(FLAlertLayer* layer, bool secondButton) override;
};

struct $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<uint32_t> m_checkpointTicks; 
        uint32_t m_physicsTicks = 0; 
        bool m_wasDeadLastFrame = false;
        bool m_saveFlowTriggered = false;
        // Guards against playEndAnimationToPos firing spuriously during
        // togglePracticeMode / resetLevel inside onInitiateRecordAction.
        bool m_isResetting = false;
        // Set true when recording starts; prevents the first transient
        // dead→alive transition (from the reset) from wiping the buffer.
        bool m_justStartedRecording = false;
        std::unordered_map<std::string, SimplePlayer*> m_ghostSprites; 
    };

    SimplePlayer* createGhostSprite(ccColor3B routeColor) {
        auto ghostSprite = SimplePlayer::create(0);
        auto playerFrame = GameManager::sharedState()->getPlayerFrame();

        if (ghostSprite) {
            ghostSprite->updatePlayerFrame(playerFrame, IconType::Cube);
            ghostSprite->setColor(routeColor);
            ghostSprite->setSecondColor(GameManager::sharedState()->colorForIdx(GameManager::sharedState()->getPlayerColor2()));
            ghostSprite->setOpacity(130);
            ghostSprite->setVisible(false);
            ghostSprite->setScale(0.9f);
            this->addChild(ghostSprite, 9999);
        }
        return ghostSprite;
    }

    void initializeRenderPool() {
        for (auto& [key, sprite] : m_fields->m_ghostSprites) {
            if (sprite) sprite->removeFromParentAndCleanup(true);
        }
        m_fields->m_ghostSprites.clear();

        auto& routes = GhostManager::get()->getActiveGhosts();
        for (auto const& ghost : routes) {
            SimplePlayer* sprite = createGhostSprite(ghost.color);
            if (sprite) {
                sprite->setVisible(ghost.isEnabled);
                m_fields->m_ghostSprites[ghost.filename] = sprite;
            }
        }
    }

    bool init(GJGameLevel* level, bool usePractice, bool isPlatformer) {
        if (!PlayLayer::init(level, usePractice, isPlatformer)) return false;

        m_fields->m_checkpointTicks.clear();
        m_fields->m_physicsTicks = 0;
        m_fields->m_wasDeadLastFrame = false;
        m_fields->m_saveFlowTriggered = false;
        m_fields->m_isResetting = false;
        m_fields->m_justStartedRecording = false;

        GhostManager::get()->loadMacroFramework(level->m_levelID);
        initializeRenderPool();
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (!m_player1) return;

        // Throttled debug log (fires every 60 frames)
        static int logThrottle = 0;
        if (logThrottle++ % 60 == 0) {
            log::info(
                "Recording={}, Buffer={}, SaveFlowTriggered={}",
                GhostManager::get()->isRecording(),
                GhostManager::get()->getRecordingBuffer().size(),
                m_fields->m_saveFlowTriggered
            );
        }

        // Handle player death
        if (m_player1->m_isDead) {
            m_fields->m_wasDeadLastFrame = true;
            return;
        }

        // Handle respawn after death — rewind recording to last checkpoint
        if (m_fields->m_wasDeadLastFrame && !m_player1->m_isDead) {
            m_fields->m_wasDeadLastFrame = false;

            // If recording just started, the player may have been in a transient
            // dead state during reset. Don't treat that as a real respawn.
            if (m_fields->m_justStartedRecording) {
                m_fields->m_justStartedRecording = false;
                // keep the fresh buffer and the tick count from the reset
            } else if (!m_fields->m_checkpointTicks.empty()) {
                m_fields->m_physicsTicks = m_fields->m_checkpointTicks.back(); 

                if (GhostManager::get()->isRecording()) {
                    auto& buffer = GhostManager::get()->getRecordingBuffer();
                    buffer.erase(std::remove_if(buffer.begin(), buffer.end(), [this](GhostFrame const& f) {
                        return f.tick > m_fields->m_physicsTicks;
                    }), buffer.end());
                }
            } else {
                m_fields->m_physicsTicks = 0;
                if (GhostManager::get()->isRecording()) GhostManager::get()->getRecordingBuffer().clear();
            }
        }

        // If recording started on a clean alive frame, the guard above never
        // fires. Make sure the one-shot startup flag is cleared so the first
        // real death/respawn later still gets normal checkpoint rewind.
        m_fields->m_justStartedRecording = false;

        // Record frame
        if (GhostManager::get()->isRecording() && !m_fields->m_saveFlowTriggered) {
            GhostManager::get()->getRecordingBuffer().push_back({
                m_fields->m_physicsTicks,
                m_player1->getPositionX(),
                m_player1->getPositionY(),
                m_player1->getRotation()
            });

            // Milestone notifications — visible proof that frames are being
            // captured mid-run without needing the Geode log. If you never see
            // the "1 frame" notification, update() isn't recording at all.
            auto sz = GhostManager::get()->getRecordingBuffer().size();
            if (sz == 1 || sz == 60 || sz == 300) {
                Notification::create(
                    fmt::format("Recording: {} frames captured", sz),
                    NotificationIcon::Info
                )->show();
            }
        }

        // Ghost playback
        auto& routes = GhostManager::get()->getActiveGhosts();
        for (auto const& ghostData : routes) {
            if (!ghostData.isEnabled || ghostData.frames.empty()) continue;

            auto it = m_fields->m_ghostSprites.find(ghostData.filename);
            if (it == m_fields->m_ghostSprites.end() || !it->second) continue;
            auto ghostSprite = it->second;

            uint32_t targetTick = m_fields->m_physicsTicks + ghostData.lookAheadTicks;

            auto itb = std::lower_bound(ghostData.frames.begin(), ghostData.frames.end(), targetTick, [](GhostFrame const& frame, uint32_t target) {
                return frame.tick < target;
            });

            if (itb != ghostData.frames.end() && itb != ghostData.frames.begin()) {
                auto ita = itb - 1;

                float tickDelta = static_cast<float>(itb->tick - ita->tick);
                float pct = (tickDelta > 0.f) ? static_cast<float>(targetTick - ita->tick) / tickDelta : 0.f;

                float lerpedX = ita->x + pct * (itb->x - ita->x);
                float lerpedY = ita->y + pct * (itb->y - ita->y);

                float diff = itb->rot - ita->rot;
                while (diff < -180.f) diff += 360.f;
                while (diff > 180.f) diff -= 360.f;
                float lerpedRot = ita->rot + pct * diff;

                ghostSprite->setVisible(true);
                ghostSprite->setPosition({lerpedX, lerpedY});
                ghostSprite->setRotation(lerpedRot);
            } else if (itb == ghostData.frames.end() && !ghostData.frames.empty()) {
                auto const& lastFrame = ghostData.frames.back();
                ghostSprite->setVisible(true);
                ghostSprite->setPosition({lastFrame.x, lastFrame.y});
                ghostSprite->setRotation(lastFrame.rot);
            } else {
                ghostSprite->setVisible(false);
            }
        }

        m_fields->m_physicsTicks++;

        // First tick complete — safe to allow save flow now. Any deferred
        // playEndAnimationToPos calls from togglePracticeMode/resetLevel fire
        // before update() ever runs (scheduler, still paused), so they are all
        // blocked by m_isResetting=true above. Clearing here means legitimate
        // end-of-run completion (which fires from within PlayLayer::update on a
        // later tick) will pass through normally.
        if (m_fields->m_physicsTicks == 1 && m_fields->m_isResetting) {
            m_fields->m_isResetting = false;
        }
    }

    void executeUnifiedSaveFlow() {
        // m_isResetting blocks the save dialog from opening during
        // togglePracticeMode / resetLevel inside onInitiateRecordAction,
        // where playEndAnimationToPos can fire spuriously.
        if (GhostManager::get()->isRecording()
            && !m_fields->m_saveFlowTriggered
            && !m_fields->m_isResetting) {
            m_fields->m_saveFlowTriggered = true;

            // Visible confirmation of how many frames were captured — helps
            // diagnose any remaining buffer-wipe issues without needing the log.
            Notification::create(
                fmt::format("Level complete! {} frames captured.", GhostManager::get()->getRecordingBuffer().size()),
                NotificationIcon::Info
            )->show();

            auto popup = GhostNameDialog::create(m_level->m_levelID, false);
            if (popup) {
                popup->show();
            }
        }
    }

    // Hook both levelComplete (normal mode) and playEndAnimationToPos
    // (practice mode — practice completion skips levelComplete in GD 2.2081).
    // The m_saveFlowTriggered guard prevents double-firing when levelComplete
    // calls playEndAnimationToPos internally on a normal completion.
    void levelComplete() {
        PlayLayer::levelComplete();
        this->executeUnifiedSaveFlow();
    }

    void playEndAnimationToPos(cocos2d::CCPoint pos) {
        PlayLayer::playEndAnimationToPos(pos);
        this->executeUnifiedSaveFlow();
    }

    void createCheckpoint() {
        PlayLayer::createCheckpoint();
        m_fields->m_checkpointTicks.push_back(m_fields->m_physicsTicks);
    }

    void removeCheckpoint(bool first) {
        PlayLayer::removeCheckpoint(first);
        if (!m_fields->m_checkpointTicks.empty()) m_fields->m_checkpointTicks.pop_back();
    }
};

void commitGhostToDiskAndMemory(int levelID, std::string const& finalName) {
    auto& buffer = GhostManager::get()->getRecordingBuffer();

    Notification::create(
        fmt::format("Saving Route... Frames Captured: {}", buffer.size()),
        NotificationIcon::Info
    )->show();

    if (buffer.empty()) {
        GhostManager::get()->clearVolatileBuffers();
        if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
            pl->m_fields->m_saveFlowTriggered = false;
            pl->m_fields->m_isResetting = false;
        }
        Notification::create("No frames were recorded. Route was not saved.", NotificationIcon::Error)->show();
        return;
    }

    auto dir = Mod::get()->getSaveDir() / std::to_string(levelID);
    std::filesystem::create_directories(dir);

    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    std::string filename = "ghost_" + std::to_string(timestamp) + ".json";
    auto macroPath = dir / filename;

    auto framesArr = matjson::Value::array();
    for (auto const& f : buffer) {
        auto fObj = matjson::Value::object();
        fObj["tick"] = static_cast<int>(f.tick);
        fObj["x"] = f.x;
        fObj["y"] = f.y;
        fObj["rot"] = f.rot;
        framesArr.push(fObj);
    }
    auto root = matjson::Value::object();
    root["frames"] = framesArr;

    std::ofstream file(macroPath);
    if (file.fail()) {
        // Must clear recording state here too — leaving isRecording=true and
        // saveFlowTriggered=true would permanently wedge the save flow.
        GhostManager::get()->clearVolatileBuffers();
        if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
            pl->m_fields->m_saveFlowTriggered = false;
            pl->m_fields->m_isResetting = false;
        }
        Notification::create("Failed to write route file! (disk full?)", NotificationIcon::Error)->show();
        return;
    }
    file << root.dump(matjson::NO_INDENTATION);
    file.close();

    RuntimeGhost newGhost;
    newGhost.name = GhostManager::get()->getUniqueRouteName(finalName, filename);
    newGhost.color = {0, 255, 255};
    newGhost.isEnabled = true;
    newGhost.filename = filename;
    newGhost.frames = buffer;

    GhostManager::get()->getActiveGhosts().push_back(newGhost);
    GhostManager::get()->saveMetadataFile(levelID);

    if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
        pl->initializeRenderPool();
    }

    GhostManager::get()->clearVolatileBuffers();
    Notification::create("Route Saved Flawlessly!", NotificationIcon::Success)->show();
}

// ---------------------------------------------------------------------------
// GhostNameDialog implementation
// ---------------------------------------------------------------------------
bool GhostNameDialog::init(int levelID, bool isRenameMode, size_t editIndex, GhostPopup* parentPopup) {
    // Pass `this` as delegate so FLAlertLayer creates its Cancel/Confirm buttons
    // normally — passing nullptr for the button strings was causing the crash
    // because GD dereferences them unconditionally to build button sprites.
    if (!FLAlertLayer::init(this, isRenameMode ? "Rename Route" : "Save Route", "", "Cancel", "Confirm", 320.f, false, 140.f, 0.8f)) {
        return false;
    }

    m_levelID = levelID;
    m_isRenameMode = isRenameMode;
    m_editIndex = editIndex;
    m_parentPopup = parentPopup;

    auto boxSize = m_mainLayer->getContentSize();

    m_inputField = geode::TextInput::create(220.f, "Route Name...", "chatFont.fnt");
    m_inputField->setPosition({ boxSize.width / 2, boxSize.height / 2 + 10.f });
    m_inputField->setMaxCharCount(22);
    m_inputField->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ ");

    if (m_isRenameMode && m_editIndex < GhostManager::get()->getActiveGhosts().size()) {
        m_inputField->setString(GhostManager::get()->getActiveGhosts()[m_editIndex].name);
    } else {
        m_inputField->setString(GhostManager::get()->getRecordingName().empty() ? "New Route" : GhostManager::get()->getRecordingName());
    }
    m_mainLayer->addChild(m_inputField);

    return true;
}

void GhostNameDialog::FLAlert_Clicked(FLAlertLayer* layer, bool secondButton) {
    // Fix (Bug 2): Break the self-retain cycle immediately. FLAlertLayer holds
    // a retained reference to its delegate (m_alertProtocol). Since `this` IS
    // that delegate, nulling it here drops the extra retain count before the
    // layer tries to dismiss itself, so it can actually be deallocated.
    m_alertProtocol = nullptr;

    if (secondButton) { // Confirm
        std::string textResult = m_inputField->getString();
        if (textResult.empty()) textResult = m_isRenameMode ? "Unnamed Route" : "New Route";

        if (m_isRenameMode) {
            if (m_editIndex < GhostManager::get()->getActiveGhosts().size()) {
                auto& targets = GhostManager::get()->getActiveGhosts();
                targets[m_editIndex].name = GhostManager::get()->getUniqueRouteName(textResult, targets[m_editIndex].filename);
                GhostManager::get()->saveMetadataFile(m_levelID);
            }
        } else {
            commitGhostToDiskAndMemory(m_levelID, textResult);
        }
    } else { // Cancel
        if (!m_isRenameMode) {
            if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
                pl->m_fields->m_saveFlowTriggered = false;
            }
            // Level is complete and update() is no longer running, so the
            // buffer can't accumulate more frames. Clear it cleanly.
            GhostManager::get()->clearVolatileBuffers();
        }
    }
    // FLAlertLayer calls keyBackClicked() automatically after this returns.
}

// ---------------------------------------------------------------------------
// GhostPopup
// ---------------------------------------------------------------------------
class GhostPopup : public FLAlertLayer, public FLAlertLayerProtocol {
private:
    int m_levelID;
    CCMenu* m_listMenu = nullptr;
    size_t m_colorEditIdx = 0;

public:
    static GhostPopup* create(int levelID) {
        auto ret = new GhostPopup();
        ret->m_levelID = levelID;
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void updateColorValue(cocos2d::ccColor3B color) {
        auto& ghostsRef = GhostManager::get()->getActiveGhosts();
        if (m_colorEditIdx < ghostsRef.size()) {
            ghostsRef[m_colorEditIdx].color = color;
            GhostManager::get()->saveMetadataFile(m_levelID);

            if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
                pl->initializeRenderPool();
            }
            this->refreshGhostListUI();
        }
    }

    void refreshGhostListUI() {
        if (!m_listMenu) return; 
        m_listMenu->removeAllChildrenWithCleanup(true);
        float yOffset = 45.f; 

        auto& ghosts = GhostManager::get()->getActiveGhosts();
        for (size_t i = 0; i < ghosts.size(); ++i) {
            auto& ghost = ghosts[i];

            auto colorSprite = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
            if (colorSprite) {
                colorSprite->setColor(ghost.color);
                colorSprite->setScale(0.6f);
                auto colBtn = CCMenuItemSpriteExtra::create(colorSprite, this, menu_selector(GhostPopup::onSelectColorPalette));
                colBtn->setTag(static_cast<int>(i));
                colBtn->setPosition({-150.f, yOffset});
                m_listMenu->addChild(colBtn);
            }

            auto onSprite = CCSprite::createWithSpriteFrameName("GJ_checkOnBtn_001.png");
            auto offSprite = CCSprite::createWithSpriteFrameName("GJ_checkOffBtn_001.png");
            if (onSprite && offSprite) {
                onSprite->setScale(0.6f);
                offSprite->setScale(0.6f);
                auto toggleBtn = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(GhostPopup::onToggleGhostVisibility));
                toggleBtn->toggle(ghost.isEnabled);
                toggleBtn->setTag(static_cast<int>(i));
                toggleBtn->setPosition({-110.f, yOffset});
                m_listMenu->addChild(toggleBtn);
            }

            auto label = CCLabelBMFont::create(ghost.name.c_str(), "bigFont.fnt");
            if (label) {
                label->setScale(0.4f);
                label->setAnchorPoint({0.f, 0.5f});
                label->setPosition({-85.f, yOffset});
                if (!ghost.isEnabled) label->setOpacity(90);
                m_listMenu->addChild(label);
            }

            auto editSprite = CCSprite::createWithSpriteFrameName("GJ_editBtn_001.png");
            if (editSprite) {
                editSprite->setScale(0.55f);
                auto editBtn = CCMenuItemSpriteExtra::create(editSprite, this, menu_selector(GhostPopup::onRenameProfileRoute));
                editBtn->setTag(static_cast<int>(i));
                editBtn->setPosition({110.f, yOffset});
                m_listMenu->addChild(editBtn);
            }

            auto delSprite = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
            if (delSprite) {
                delSprite->setScale(0.55f);
                auto delBtn = CCMenuItemSpriteExtra::create(delSprite, this, menu_selector(GhostPopup::onDeleteProfileRecord));
                delBtn->setTag(static_cast<int>(i));
                delBtn->setPosition({145.f, yOffset});
                m_listMenu->addChild(delBtn);
            }

            yOffset -= 35.f;
        }
    }

    bool init() override {
        if (!FLAlertLayer::init(this, "Ghost Manager", "", "Close", nullptr, 380.f, false, 250.f, 1.f)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_listMenu = CCMenu::create();
        m_listMenu->setPosition({winSize.width / 2, winSize.height / 2 + 20.f});
        
        m_listMenu->setTouchPriority(-141); 
        m_mainLayer->addChild(m_listMenu);

        this->refreshGhostListUI();

        if (m_buttonMenu) {
            auto recBtnSprite = ButtonSprite::create("Record New Route", "goldFont.fnt", "GJ_button_01.png");
            if (recBtnSprite) {
                auto recBtn = CCMenuItemSpriteExtra::create(recBtnSprite, this, menu_selector(GhostPopup::onInitiateRecordAction));
                recBtn->setPosition({0.f, -45.f});
                m_buttonMenu->addChild(recBtn);
            }
        }

        return true;
    }

    void onInitiateRecordAction(CCObject*) {
        // Capture pl/gpl FIRST, before any UI teardown or resets, so we
        // always have a valid pointer even if keyBackClicked / togglePracticeMode
        // causes intermediate scene-management side effects.
        auto pl = PlayLayer::get();
        if (!pl) return;
        auto gpl = static_cast<GhostPlayLayer*>(pl);

        GhostManager::get()->setRecording(true);
        GhostManager::get()->getRecordingBuffer().clear();

        // Raise reset guard before any internal resets fire so that
        // playEndAnimationToPos / levelComplete hooks can't open the save
        // dialog while we're still setting up the recording session.
        if (gpl) gpl->m_fields->m_isResetting = true;

        if (!pl->m_isPracticeMode) {
            // togglePracticeMode(true) already calls resetLevel() internally
            // in GD 2.2081 — do NOT call resetLevel() again after this or the
            // player can land in a transient dead/spawn state on the first update.
            pl->togglePracticeMode(true);
        } else {
            // Already in practice mode — need a manual reset.
            pl->resetLevel();
        }

        // Re-fetch PlayLayer after the reset — togglePracticeMode can recreate
        // the PlayLayer node entirely, making the original `gpl` pointer stale.
        // IMPORTANT: m_isResetting stays TRUE here. GD can schedule a deferred
        // playEndAnimationToPos call that fires on the next scheduler tick
        // (even while the PauseLayer is still open). If we cleared isResetting
        // now, that deferred call would race through executeUnifiedSaveFlow with
        // 0 frames and set saveFlowTriggered=true before the player even moves.
        // Instead we clear isResetting in update() after the first physics tick
        // completes — update() only runs once the game is truly unpaused.
        if (auto freshPl = PlayLayer::get()) {
            auto freshGpl = static_cast<GhostPlayLayer*>(freshPl);
            freshGpl->m_fields->m_isResetting        = true; // cleared in update() tick 1
            freshGpl->m_fields->m_saveFlowTriggered  = false;
            freshGpl->m_fields->m_physicsTicks        = 0;
            freshGpl->m_fields->m_checkpointTicks.clear();
            freshGpl->m_fields->m_wasDeadLastFrame    = false;
            freshGpl->m_fields->m_justStartedRecording = true;
        }

        Notification::create("Recording started! Play to the end to save.", NotificationIcon::Success)->show();

        // Close GhostPopup last. At this point game state is fully prepared,
        // so when the user dismisses the PauseLayer the scheduler resumes and
        // update() immediately starts capturing frames with recording = true.
        this->keyBackClicked();
    }

    void onToggleGhostVisibility(CCObject* sender) {
        auto toggler = static_cast<CCMenuItemToggler*>(sender);
        size_t idx = static_cast<size_t>(toggler->getTag());
        auto& ghosts = GhostManager::get()->getActiveGhosts();
        if (idx >= ghosts.size()) return;

        ghosts[idx].isEnabled = !ghosts[idx].isEnabled; 
        GhostManager::get()->saveMetadataFile(m_levelID);

        if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
            pl->initializeRenderPool();
        }
        this->refreshGhostListUI();
    }

    void onSelectColorPalette(CCObject* sender) {
        size_t idx = static_cast<size_t>(sender->getTag());
        auto& ghosts = GhostManager::get()->getActiveGhosts();
        if (idx >= ghosts.size()) return;

        m_colorEditIdx = idx;

        // Fix (color picker use-after-free): retain self for the duration of
        // the callback so GhostPopup cannot be freed while the ColorPickPopup
        // is still open and the lambda holds a raw `this` pointer.
        this->retain();
        auto popup = ColorPickPopup::create(ghosts[idx].color);
        popup->setCallback([this](cocos2d::ccColor4B color) {
            this->updateColorValue({color.r, color.g, color.b});
            this->release(); // paired with the retain above
        });
        popup->show();
    }

    void onRenameProfileRoute(CCObject* sender) {
        size_t idx = static_cast<size_t>(sender->getTag());
        if (idx >= GhostManager::get()->getActiveGhosts().size()) return;

        auto inputPopup = GhostNameDialog::create(m_levelID, true, idx, this);
        if (inputPopup) {
            inputPopup->show();
        }
    }

    void FLAlert_Clicked(FLAlertLayer* layer, bool secondButton) override {
        this->refreshGhostListUI();
    }

    void onDeleteProfileRecord(CCObject* sender) {
        size_t idx = static_cast<size_t>(sender->getTag());
        auto& ghosts = GhostManager::get()->getActiveGhosts();
        if (idx >= ghosts.size()) return;

        std::string targetFilename = ghosts[idx].filename;
        auto dir = Mod::get()->getSaveDir() / std::to_string(m_levelID);
        auto targetFile = dir / targetFilename;

        std::error_code ec;
        if (std::filesystem::exists(targetFile)) {
            std::filesystem::remove(targetFile, ec);
        }

        if (!ec) {
            ghosts.erase(ghosts.begin() + idx);
            GhostManager::get()->saveMetadataFile(m_levelID);

            if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
                pl->initializeRenderPool();
            }
        }
        this->refreshGhostListUI();
    }

    static void open(int levelID) {
        auto p = GhostPopup::create(levelID);
        if (p) p->show();
    }
};

struct $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByType<CCMenu>(0); 
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        if (menu) {
            auto managerBtnSprite = CCSprite::createWithSpriteFrameName("GJ_downloadsIcon_001.png");
            if (managerBtnSprite) {
                auto managerBtn = CCMenuItemSpriteExtra::create(managerBtnSprite, this, menu_selector(MyPauseLayer::onOpenGhostConfigPanel));
                menu->addChild(managerBtn);
                menu->updateLayout();
            }
        }

        if (GhostManager::get()->isRecording() && !GhostManager::get()->getRecordingBuffer().empty()) {
            auto manualSaveMenu = CCMenu::create();
            manualSaveMenu->setPosition({winSize.width / 2, winSize.height / 2 + 55.f});
            this->addChild(manualSaveMenu, 1000);

            auto saveBtnSprite = ButtonSprite::create("Finish & Save Route", "goldFont.fnt", "GJ_button_02.png");
            if (saveBtnSprite) {
                auto saveBtn = CCMenuItemSpriteExtra::create(saveBtnSprite, this, menu_selector(MyPauseLayer::onManualSaveOverrideAction));
                manualSaveMenu->addChild(saveBtn);
            }
        }
    }

    void onOpenGhostConfigPanel(CCObject*) {
        if (auto pl = PlayLayer::get()) {
            GhostPopup::open(pl->m_level->m_levelID);
        }
    }

    void onManualSaveOverrideAction(CCObject*) {
        if (auto pl = PlayLayer::get()) {
            if (auto gpl = static_cast<GhostPlayLayer*>(pl)) {
                gpl->executeUnifiedSaveFlow();
            }
            this->keyBackClicked();
        }
    }
};
