#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <thread>

using namespace geode::prelude;

// ==========================================
// 🏗️ DATA MODEL DEFINITIONS (PURE DATA)
// ==========================================
struct GhostFrame {
    uint32_t tick; 
    float x, y, rot;
};

struct RuntimeGhost {
    std::string name;
    ccColor3B color;
    bool isEnabled;
    std::string filename; // 🔒 Used as our permanent, immutable Unique ID
    uint32_t lookAheadTicks = 0; 
    std::vector<GhostFrame> frames;
};

// ==========================================
// 🎛️ THE GHOST DATA MANAGER (SINGLETON)
// ==========================================
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

    std::string getUniqueRouteName(std::string const& baseName) {
        std::string uniqueName = baseName;
        int counter = 2;
        bool exists = true;
        while (exists) {
            exists = false;
            for (auto const& ghost : m_activeGhosts) {
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

    // 🧵 Optimization: Non-blocking asynchronous metadata serialization
    void saveMetadataFile(int levelID) {
        auto dir = Mod::get()->getSaveDir() / std::to_string(levelID);
        std::filesystem::create_directories(dir);
        auto metaPath = dir / "metadata.json";

        auto ghostsArr = matjson::Array();
        for (auto const& g : m_activeGhosts) {
            auto ghostObj = matjson::Object();
            ghostObj["name"] = g.name;
            ghostObj["enabled"] = g.isEnabled;
            ghostObj["filename"] = g.filename;
            ghostObj["lookahead_ticks"] = static_cast<int>(g.lookAheadTicks);
            
            auto colArr = matjson::Array();
            colArr.push_back(static_cast<int>(g.color.r));
            colArr.push_back(static_cast<int>(g.color.g));
            colArr.push_back(static_cast<int>(g.color.b));
            ghostObj["color"] = colArr;

            ghostsArr.push_back(ghostObj);
        }

        auto root = matjson::Object();
        root["ghosts"] = ghostsArr;

        std::string metaStr = matjson::Value(root).dump(matjson::JSONFormat::Indented);
        
        std::thread([metaPath, metaStr]() {
            std::ofstream file(metaPath);
            if (!file.fail()) {
                file << metaStr;
            }
            file.close();
        }).detach();
    }

    void loadMacroFramework(int levelID) {
        m_activeGhosts.clear();
        auto dir = Mod::get()->getSaveDir() / std::to_string(levelID);
        auto metaPath = dir / "metadata.json";

        if (!std::filesystem::exists(metaPath)) return;

        std::ifstream metaFile(metaPath);
        if (metaFile.fail()) return;

        std::string metaStr((std::istreambuf_iterator<char>(metaFile)), std::istreambuf_iterator<char>());
        auto metaJson = matjson::parse(metaStr).unwrapOrDefault();
        metaFile.close();

        if (!metaJson.contains("ghosts") || !metaJson["ghosts"].is_array()) return;

        for (auto const& g : metaJson["ghosts"].as_array()) {
            if (!g.contains("name") || !g.contains("filename")) continue;

            RuntimeGhost ghost;
            ghost.name = g["name"].as_string();
            ghost.isEnabled = g.contains("enabled") ? g["enabled"].as_bool() : true;
            ghost.filename = g["filename"].as_string();
            ghost.lookAheadTicks = g.contains("lookahead_ticks") ? static_cast<uint32_t>(g["lookahead_ticks"].as_int()) : 0;

            if (g.contains("color") && g["color"].is_array() && g["color"].as_array().size() >= 3) {
                auto colorArr = g["color"].as_array();
                ghost.color.r = static_cast<uint8_t>(colorArr[0].as_int());
                ghost.color.g = static_cast<uint8_t>(colorArr[1].as_int());
                ghost.color.b = static_cast<uint8_t>(colorArr[2].as_int());
            } else {
                ghost.color = {0, 255, 255}; 
            }

            auto macroPath = dir / ghost.filename;
            if (std::filesystem::exists(macroPath)) {
                std::ifstream macroFile(macroPath);
                if (!macroFile.fail()) {
                    std::string macroStr((std::istreambuf_iterator<char>(macroFile)), std::istreambuf_iterator<char>());
                    auto macroJson = matjson::parse(macroStr).unwrapOrDefault();

                    if (macroJson.contains("frames") && macroJson["frames"].is_array()) {
                        for (auto const& f : macroJson["frames"].as_array()) {
                            if (!f.contains("tick") || !f.contains("x") || !f.contains("y") || !f.contains("rot")) continue;
                            if (!f["tick"].is_number() || !f["x"].is_number() || !f["y"].is_number() || !f["rot"].is_number()) continue;

                            ghost.frames.push_back({
                                static_cast<uint32_t>(f["tick"].as_int()),
                                static_cast<float>(f["x"].as_double()),
                                static_cast<float>(f["y"].as_double()),
                                static_cast<float>(f["rot"].as_double())
                            });
                        }
                    }
                }
                macroFile.close();
            }
            m_activeGhosts.push_back(ghost);
        }
    }
};

// ==========================================
// 💬 DIALOG INTERFACE POPUPS
// ==========================================
class GhostNameDialog : public FLAlertLayer, public TextInputDelegate {
private:
    int m_levelID;
    size_t m_editIndex;
    bool m_isRenameMode;
    TextInput* m_inputField = nullptr;

public:
    static GhostNameDialog* create(int levelID, bool isRenameMode, size_t editIndex = 0) {
        auto ret = new GhostNameDialog();
        ret->m_levelID = levelID;
        ret->m_isRenameMode = isRenameMode;
        ret->m_editIndex = editIndex;
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        float width = 320.f;
        float height = 160.f;
        if (!FLAlertLayer::init(this, m_isRenameMode ? "Rename Route" : "Save Route", "Cancel", "Confirm", width, false, height, 1.f)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_inputField = TextInput::create(220.f, "Route Name...");
        m_inputField->setPosition({winSize.width / 2, winSize.height / 2 + 10.f});
        m_inputField->setDelegate(this);
        
        if (m_isRenameMode && m_editIndex < GhostManager::get()->getActiveGhosts().size()) {
            m_inputField->setString(GhostManager::get()->getActiveGhosts()[m_editIndex].name);
        } else {
            m_inputField->setString(GhostManager::get()->getRecordingName().empty() ? "New Route" : GhostManager::get()->getRecordingName());
        }
        
        m_mainLayer->addChild(m_inputField);
        return true;
    }

    void FLAlert_Clicked(FLAlertLayer*, bool btn2) override;
};

// ==========================================
// 🕹️ DECOUPLED SIMULATION ENGINE HOOK MATRIX
// ==========================================
struct $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<uint32_t> m_checkpointTicks; 
        uint32_t m_physicsTicks = 0; 
        bool m_wasDeadLastFrame = false;
        bool m_saveFlowTriggered = false; 
        // 🔒 Robust Identifier Map: Associates unique filenames to their active render elements
        std::unordered_map<std::string, SimplePlayer*> m_ghostSprites; 
    };

    // Centralized Single-Responsibility factory logic for generating sprites
    SimplePlayer* createGhostSprite(ccColor3B routeColor) {
        auto playerFrame = GameManager::sharedState()->getPlayerFrame();
        auto ghostSprite = SimplePlayer::create(playerFrame);
        if (ghostSprite) {
            ghostSprite->setColor(routeColor);
            ghostSprite->setSecondColor(GameManager::sharedState()->getPlayerColor2());
            ghostSprite->setOpacity(130);
            ghostSprite->setVisible(false);
            ghostSprite->setScale(0.9f);
            this->addChild(ghostSprite, 9999);
        }
        return ghostSprite;
    }

    // ⚡ High-Speed Visibility Controls (Avoids costly node reconstruction cycles)
    void updateGhostVisibility(std::string const& filename, bool enabled) {
        if (m_fields->m_ghostSprites.contains(filename)) {
            auto sprite = m_fields->m_ghostSprites[filename];
            if (sprite) sprite->setVisible(enabled);
        }
    }

    // Safe dynamic node extraction on file purges
    void removeGhostSprite(std::string const& filename) {
        if (m_fields->m_ghostSprites.contains(filename)) {
            auto sprite = m_fields->m_ghostSprites[filename];
            if (sprite) sprite->removeFromParentAndCleanup(true);
            m_fields->m_ghostSprites.erase(filename);
        }
    }

    void syncGhostSpriteColor(std::string const& filename, ccColor3B color) {
        if (m_fields->m_ghostSprites.contains(filename)) {
            auto sprite = m_fields->m_ghostSprites[filename];
            if (sprite) sprite->setColor(color);
        }
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

        GhostManager::get()->loadMacroFramework(level->m_levelID);
        initializeRenderPool();
        return true;
    }

    void processCommands() override {
        PlayLayer::processCommands();
        if (!m_player1) return;

        if (m_player1->m_isDead) {
            m_fields->m_wasDeadLastFrame = true;
            return;
        }

        if (m_fields->m_wasDeadLastFrame && !m_player1->m_isDead) {
            m_fields->m_wasDeadLastFrame = false;
            if (!m_fields->m_checkpointTicks.empty()) {
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

        if (GhostManager::get()->isRecording() && m_glInPracticeMode) {
            GhostManager::get()->getRecordingBuffer().push_back({
                m_fields->m_physicsTicks,
                m_player1->getPositionX(),
                m_player1->getPositionY(),
                m_player1->getRotation()
            });
        }

        m_fields->m_physicsTicks++;
    }

    void update(float dt) override {
        PlayLayer::update(dt);
        if (!m_player1 || m_player1->m_isDead) return;

        auto& routes = GhostManager::get()->getActiveGhosts();
        for (auto const& ghostData : routes) {
            if (!ghostData.isEnabled || ghostData.frames.empty()) continue;

            // ⚡ O(1) Stable Pointer Lookup from Map Structure
            if (!m_fields->m_ghostSprites.contains(ghostData.filename)) continue;
            auto ghostSprite = m_fields->m_ghostSprites[ghostData.filename];
            if (!ghostSprite) continue;

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
    }

    void registerDynamicRecordSprite(std::string const& filename, ccColor3B color) {
        auto sprite = createGhostSprite(color);
        if (sprite) {
            sprite->setVisible(true);
            m_fields->m_ghostSprites[filename] = sprite;
        }
    }

    void executeUnifiedSaveFlow() {
        if (GhostManager::get()->isRecording() && !m_fields->m_saveFlowTriggered) {
            m_fields->m_saveFlowTriggered = true; 
            GhostManager::get()->setRecording(false);
            auto popup = GhostNameDialog::create(m_level->m_levelID, false);
            if (popup) popup->show();
        }
    }

    void playEndAnimationToPos(cocos2d::CCPoint pos) override {
        PlayLayer::playEndAnimationToPos(pos);
        if (m_glInPracticeMode) this->executeUnifiedSaveFlow();
    }

    void levelComplete() override {
        PlayLayer::levelComplete();
        this->executeUnifiedSaveFlow();
    }

    void onQuit() override {
        GhostManager::get()->clearVolatileBuffers();
        PlayLayer::onQuit();
    }

    void createCheckpoint() override {
        PlayLayer::createCheckpoint();
        m_fields->m_checkpointTicks.push_back(m_fields->m_physicsTicks);
    }

    void removeLastCheckpoint() override {
        PlayLayer::removeLastCheckpoint();
        if (!m_fields->m_checkpointTicks.empty()) m_fields->m_checkpointTicks.pop_back();
    }
};

// ==========================================
// 💬 POST-DIALOG DECLARATION ATTACHMENTS
// ==========================================
void commitGhostToDiskAndMemory(int levelID, std::string const& finalName) {
    auto& buffer = GhostManager::get()->getRecordingBuffer();
    if (buffer.empty()) return;

    auto dir = Mod::get()->getSaveDir() / std::to_string(levelID);
    std::filesystem::create_directories(dir);

    std::string filename = "ghost_" + std::to_string(geode::utils::time::getMillis()) + ".json";
    auto macroPath = dir / filename;

    auto framesArr = matjson::Array();
    for (auto const& f : buffer) {
        auto fObj = matjson::Object();
        fObj["tick"] = static_cast<int>(f.tick);
        fObj["x"] = f.x;
        fObj["y"] = f.y;
        fObj["rot"] = f.rot;
        framesArr.push_back(fObj);
    }
    auto root = matjson::Object();
    root["frames"] = framesArr;

    // 🧵 Optimization: Offload stringified JSON data saving to background execution workers
    std::string jsonStr = matjson::Value(root).dump(matjson::JSONFormat::Compact);
    
    std::thread([macroPath, jsonStr]() {
        std::ofstream file(macroPath);
        if (!file.fail()) {
            file << jsonStr;
        }
        file.close();
    }).detach();

    RuntimeGhost newGhost;
    newGhost.name = GhostManager::get()->getUniqueRouteName(finalName);
    newGhost.color = {0, 255, 255};
    newGhost.isEnabled = true;
    newGhost.filename = filename;
    newGhost.frames = buffer;
    
    GhostManager::get()->getActiveGhosts().push_back(newGhost);
    GhostManager::get()->saveMetadataFile(levelID);
    
    if (auto pl = static_cast<GhostPlayLayer*>(PlayLayer::get())) {
        pl->registerDynamicRecordSprite(filename, newGhost.color);
    }

    GhostManager::get()->clearVolatileBuffers();
    Notification::create("Route Saved Flawlessly!", NotificationIcon::Success)->show();
}

void GhostNameDialog::FLAlert_Clicked(FLAlertLayer*, bool btn2) {
    if (btn2) {
        std::string textResult = m_inputField->getString();
        if (textResult.empty()) textResult = "Unnamed Route";

        if (m_isRenameMode) {
            if (m_editIndex < GhostManager::get()->getActiveGhosts().size()) {
                GhostManager::get()->getActiveGhosts()[m_editIndex].name = GhostManager::get()->getUniqueRouteName(textResult);
                GhostManager::get()->saveMetadataFile(m_levelID);
            }
        } else {
            commitGhostToDiskAndMemory(m_levelID, textResult);
        }
    }
    this->onClose(nullptr);
}

// ==========================================
// 🎛️ SYSTEM DASHBOARD POPUP INTERFACE
// ==========================================
class GhostPopup : public FLAlertLayer, public FLAlertLayerProtocol {
private:
    int m_levelID;
    CCMenu* m_listMenu = nullptr;

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

    bool init() override {
        if (!FLAlertLayer::init(this, "Ghost Manager", "Close", nullptr, nullptr, 380.f, false, 250.f, 1.f)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_listMenu = CCMenu::create();
        