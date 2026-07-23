#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include "GhostDebug.hpp"

using namespace geode::prelude;

// =====================================================================
// DATA STRUCTURES
// =====================================================================

struct GhostFrame {
    float time; // seconds since recording started (or since last checkpoint rewind)
    float x;
    float y;
    float rotation;
};

struct RuntimeGhost {
    std::string routeName;
    std::string filename;
    cocos2d::ccColor3B color = {255, 255, 255};
    bool enabled = true;
    float lookAheadSeconds = 0.0f;

    std::vector<GhostFrame> frames;

    // Playback-only, not saved to disk
    size_t playbackIndex = 0;
    float playbackElapsedTime = 0.0f;
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
    bool m_updateEverPinged = false; // debug-only: proves the update hook is being called at all
    float m_recordingElapsedTime = 0.0f;
    int m_levelID = -1;

    // Clamp any single frame's dt so a pause-menu / loading-hitch dt spike
    // can't instantly jump the recording/playback clock forward by several
    // real seconds in one update. This was the cause of the ghost doing a
    // "half jump and disappear" - a huge unclamped dt after unpausing threw
    // the elapsed-time clock way past the end of the route in one frame.
    static float clampDt(float dt) {
        return std::min(dt, 0.1f);
    }

    std::filesystem::path saveDirForLevel() {
        auto base = Mod::get()->getSaveDir() / "ghosts" / std::to_string(m_levelID);
        std::filesystem::create_directories(base);
        return base;
    }

    // ---------------- Recording ----------------

    void startRecording() {
        m_recordingBuffer.clear();
        m_checkpointFrameCounts.clear();
        m_recordingElapsedTime = 0.0f;
        m_isRecording = true;
        m_saveFlowTriggered = false;
        GhostDebug::popup("Ghost Debug", "Recording started");
    }

    void stopRecording() {
        m_isRecording = false;
        GhostDebug::frameCount("Recording stopped", m_recordingBuffer.size());
    }

    void recordFrame(float dt, float x, float y, float rotation) {
        if (!m_isRecording) return;
        m_recordingElapsedTime += clampDt(dt);
        m_recordingBuffer.push_back({m_recordingElapsedTime, x, y, rotation});

        if (!m_firstFramePinged) {
            m_firstFramePinged = true;
            GhostDebug::popup("Ghost Debug", "recordFrame() confirmed working - first frame captured");
        }
    }

    void onCheckpointCreated() {
        if (!m_isRecording) return;
        m_checkpointFrameCounts.push_back(m_recordingBuffer.size());
    }

    // Used for a full level restart (normal mode has no checkpoints to
    // rewind to - the whole attempt starts over from scratch).
    void fullReset() {
        if (!m_isRecording) return;
        m_recordingBuffer.clear();
        m_checkpointFrameCounts.clear();
        m_recordingElapsedTime = 0.0f;
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
        } else {
            m_recordingBuffer.clear();
        }

        // Resume the elapsed-time clock from wherever the kept frames leave off,
        // so future frames keep growing consistently instead of jumping backward.
        m_recordingElapsedTime = m_recordingBuffer.empty() ? 0.0f : m_recordingBuffer.back().time;

        GhostDebug::popup("Ghost Debug",
            "Rewound from " + std::to_string(before) +
            " to " + std::to_string(m_recordingBuffer.size()) + " frames");
    }

    // ---------------- Save / Load ----------------

    // Metadata for all routes on this level: name, color, enabled, filename, lookahead.
    void saveMetadata(bool showDebugPopup = true) {
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
            entry["lookAheadSeconds"] = ghost.lookAheadSeconds;
            root.push(entry);
        }

        std::ofstream file(path);
        if (file.is_open()) {
            file << root.dump();
            file.close();
            if (showDebugPopup) {
                GhostDebug::popup("Ghost Debug", "Metadata saved (" + std::to_string(m_loadedGhosts.size()) + " routes)");
            }
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
            ghost.lookAheadSeconds = entry["lookAheadSeconds"].asDouble().unwrapOr(0.0);

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
            frame.time = f["time"].asDouble().unwrapOr(0.0);
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

        if (m_recordingBuffer.empty()) {
            GhostDebug::popup("Ghost Debug ERROR", "Recording buffer is EMPTY - nothing to save. Check that recordFrame() is actually being called in update().");
            return;
        }

        size_t frameCountSaved = m_recordingBuffer.size();
        std::string filename = routeName + "_" + std::to_string(std::rand()) + ".json";

        matjson::Value root = matjson::Value::object();
        matjson::Value framesArr = matjson::Value::array();
        for (auto& f : m_recordingBuffer) {
            matjson::Value entry = matjson::Value::object();
            entry["time"] = f.time;
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
        newGhost.lookAheadSeconds = 0.0f;
        newGhost.frames = m_recordingBuffer;
        m_loadedGhosts.push_back(newGhost);

        saveMetadata(false); // false = don't show its own popup, we show one consolidated message below

        GhostDebug::popup("Ghost Debug",
            "Route '" + routeName + "' saved successfully (" + std::to_string(frameCountSaved) + " frames)");
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
        auto gm = GameManager::sharedState();
        int iconID = gm->getPlayerFrame();
        auto color1 = gm->colorForIdx(gm->getPlayerColor());
        auto color2 = gm->colorForIdx(gm->getPlayerColor2());

        for (auto& ghost : m_loadedGhosts) {
            if (!ghost.enabled) continue;

            ghost.sprite = SimplePlayer::create(iconID);
            if (ghost.sprite) {
                ghost.sprite->updatePlayerFrame(iconID, IconType::Cube);
                ghost.sprite->setColors(color1, color2);
                // ghost.color (per-route tint set via the color-cycle button)
                // overlays a semi-transparent wash on top of the real icon
                // colors, so routes stay visually distinguishable from each other.
                ghost.sprite->setOpacity(150);
                ghost.playbackIndex = 0;
                ghost.playbackElapsedTime = 0.0f;
                parentLayer->addChild(ghost.sprite);
            } else {
                GhostDebug::popup("Ghost Debug ERROR", "Failed to create SimplePlayer for " + ghost.routeName);
            }
        }
    }

    void tickPlayback(float dt) {
        float clamped = clampDt(dt);
        for (auto& ghost : m_loadedGhosts) {
            if (!ghost.enabled || !ghost.sprite || ghost.frames.empty()) continue;

            ghost.playbackElapsedTime += clamped;
            float targetTime = ghost.playbackElapsedTime + ghost.lookAheadSeconds;

            // Advance the cursor forward while the next frame's timestamp has
            // already passed - frames are chronological, so this only ever
            // moves forward, staying in sync regardless of current framerate.
            while (ghost.playbackIndex + 1 < ghost.frames.size() &&
                   ghost.frames[ghost.playbackIndex + 1].time <= targetTime) {
                ghost.playbackIndex++;
            }

            auto& frame = ghost.frames[ghost.playbackIndex];
            ghost.sprite->setPosition({frame.x, frame.y});
            ghost.sprite->setRotation(frame.rotation);

            if (ghost.playbackElapsedTime > ghost.frames.back().time) {
                ghost.sprite->setVisible(false);
            }
        }
    }

    // Resets every ghost's playback position - called on respawn (both
    // full-level restart and practice-mode checkpoint reload) so ghosts
    // play from the start again instead of staying hidden after one death.
    void resetPlayback() {
        for (auto& ghost : m_loadedGhosts) {
            ghost.playbackIndex = 0;
            ghost.playbackElapsedTime = 0.0f;
            if (ghost.sprite) ghost.sprite->setVisible(ghost.enabled);
        }
    }
};
