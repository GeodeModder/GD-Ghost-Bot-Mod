#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "GhostDebug.hpp"
#include "GhostTypes.hpp"
#include "GhostPopups.hpp"
#include "GhostAccess.hpp"

using namespace geode::prelude;

// =====================================================================
// GHOST PLAY LAYER
// Single merged $modify(PlayLayer) hook - owns the GhostManager instance
// for this play session via Fields. Handles init (load/spawn ghosts),
// checkpoints (rewind recording to match), death/respawn, and the
// post-completion save flow.
//
// The GJBaseGameLayer::update() hook (actual per-frame recording/playback)
// lives in GhostUpdateHook.cpp instead - see the comment there for why.
// =====================================================================
class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        GhostManager m_ghostManager;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        m_fields->m_ghostManager.m_levelID = level ? level->m_levelID : -1;
        m_fields->m_ghostManager.loadMetadataAndGhosts();
        m_fields->m_ghostManager.respawnAllSprites(m_objectLayer);

        return true;
    }

    CheckpointObject* createCheckpoint() {
        auto result = PlayLayer::createCheckpoint();
        m_fields->m_ghostManager.onCheckpointCreated();
        return result;
    }

    // Hides ghost sprites the instant the real player dies, so they don't
    // keep floating there looking unaffected. The actual reset-to-start
    // happens in loadFromCheckpoint (practice mode) or resetLevel (normal
    // mode) below, whichever fires for this death.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);

        for (auto& ghost : m_fields->m_ghostManager.m_loadedGhosts) {
            if (ghost.sprite) ghost.sprite->setVisible(false);
        }
    }

    void removeCheckpoint(bool first) {
        PlayLayer::removeCheckpoint(first);
        m_fields->m_ghostManager.onCheckpointRemoved();
    }

    // Practice mode: rewind recording to the last checkpoint, respawn ghosts
    // from the start of their route so they play alongside you again.
    // Also re-spawns the sprites themselves (not just resets playback) since
    // GD's checkpoint reload may tear down and rebuild parts of the object
    // layer, potentially invalidating our old sprite pointer.
    void loadFromCheckpoint(CheckpointObject* object) {
        PlayLayer::loadFromCheckpoint(object);
        m_fields->m_ghostManager.rewindToLastCheckpoint();
        m_fields->m_ghostManager.respawnAllSprites(nullptr);
        m_fields->m_ghostManager.resetPlayback();
    }

    // Normal mode: no checkpoints exist, so a death means a full level
    // restart. GD's resetLevel() rebuilds the object layer, which silently
    // destroyed our ghost sprite before (that was the "disappears and never
    // comes back" bug) - respawning fresh sprites here fixes it.
    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->m_ghostManager.fullReset();
        m_fields->m_ghostManager.respawnAllSprites(nullptr);
        m_fields->m_ghostManager.resetPlayback();
    }

    // Entering/leaving practice mode likely resets the level similarly to
    // a normal retry, which was silently destroying the ghost sprite before
    // it ever got a chance to appear - respawn fresh sprites here too.
    void togglePracticeMode(bool practice) {
        PlayLayer::togglePracticeMode(practice);
        m_fields->m_ghostManager.respawnAllSprites(nullptr);
        m_fields->m_ghostManager.resetPlayback();
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

// Implementation of the shared accessor declared in GhostAccess.hpp.
// This is the ONE place the GhostPlayLayer type is used outside its own
// definition, and it's safe here since it's the same translation unit.
GhostManager* ghostGetCurrentManager() {
    auto playLayer = static_cast<GhostPlayLayer*>(PlayLayer::get());
    if (!playLayer) return nullptr;
    return &playLayer->m_fields->m_ghostManager;
}
