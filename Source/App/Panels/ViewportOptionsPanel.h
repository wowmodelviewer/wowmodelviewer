#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class OrbitCamera;
class Renderer;
class WoWModel;

namespace ViewportOptionsPanel
{

// ---- Types shared with the rest of the application -----------------------

struct GeosetEntry
{
    size_t   index = 0;  // index into model->geosets[]
    uint32_t id    = 0;  // geoset id
    std::string label;
};

struct GeosetGroupEntry
{
    std::string name;
    size_t meshId = 0;
    std::vector<GeosetEntry> geosets;
};

struct ParticleColorState
{
    bool  enabled           = false;
    float colors[3][3][3]   = {}; // [set 0..2 for IDs 11,12,13][phase 0..2][r/g/b]
    bool  hasSet[3]         = {}; // which color IDs (11,12,13) are present
};

// ---- Per-frame context passed by the caller ------------------------------

struct DrawContext
{
    // Renderer
    Renderer* renderer = nullptr;

    // Settings
    bool*       drawGrid = nullptr;
    glm::vec3*  bgColor  = nullptr;

    // Camera
    OrbitCamera* camera = nullptr;

    // Model accessor
    std::function<WoWModel*()> getLoadedModel;

    // Model-control state (pointers into AppState)
    std::vector<GeosetGroupEntry>* geosetGroups = nullptr;
    ParticleColorState*            pcrState      = nullptr;

    // Skin state (needed for particle-color reset)
    int* selectedSkin = nullptr;
    std::function<void(WoWModel*, int)> applySkin;

    // Camera reset
    std::function<void()> resetCamera;
};

/// Draw the Viewport Options panel contents (call between Begin / End).
void draw(DrawContext& ctx);

} // namespace ViewportOptionsPanel
