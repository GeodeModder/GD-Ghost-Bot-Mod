#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "GhostDebug.hpp"
#include "GhostAccess.hpp"

using namespace geode::prelude;

// =====================================================================
// GHOST BASE GAME LAYER
// PlayLayer::update never actually got hooked (confirmed via the load-time
// hook log - it was silently missing while every other override succeeded).
// The real per-frame game loop lives one level up, on GJBaseGameLayer,
// PlayLayer's parent class. We hook update() here instead.
//
// This file only needs ghostGetCurrentManager() and PlayLayer::get() - it
// never needs to know about the GhostPlayLayer type itself, which is what
// lets this live in its own file separate from GhostPlayLayer.cpp.
// =====================================================================
class $modify(GhostBaseGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);

        // Only makes sense during actual gameplay, not the level editor -
        // PlayLayer::get() returns null when not in a real play session.
        auto playLayer = PlayLayer::get();
        if (!playLayer) return;

        auto manager = ghostGetCurrentManager();
        if (!manager) return;

        if (!manager->m_updateEverPinged) {
            manager->m_updateEverPinged = true;
            GhostDebug::popup("Ghost Debug", "GJBaseGameLayer::update() IS running (unconditional check)");
        }

        if (manager->m_isRecording && !manager->m_updateDiagnosticPinged) {
            manager->m_updateDiagnosticPinged = true;
            GhostDebug::popup("Ghost Debug",
                std::string("update() reached, isRecording=true, m_player1 is ") +
                (playLayer->m_player1 ? "VALID" : "NULL"));
        }

        if (manager->m_isRecording && playLayer->m_player1) {
            manager->recordFrame(
                dt,
                playLayer->m_player1->m_position.x,
                playLayer->m_player1->m_position.y,
                playLayer->m_player1->getRotation()
            );
        }

        manager->tickPlayback(dt);
    }
};
