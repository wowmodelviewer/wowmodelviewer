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

/// @brief Single geoset entry referencing a model geoset by index and id.
struct GeosetEntry
{
    size_t   index = 0;  ///< Index into model->geosets[].
    uint32_t id    = 0;  ///< Geoset id.
    std::string label;   ///< Human-readable label for the geoset.
};

/// @brief A named group of geosets sharing the same mesh id.
struct GeosetGroupEntry
{
    std::string name;                    ///< Display name for the group.
    size_t meshId = 0;                   ///< Shared mesh id for geosets in this group.
    std::vector<GeosetEntry> geosets;    ///< Geosets belonging to this group.
};

/// @brief Editable state for particle colour replacement (colour IDs 11, 12, 13).
struct ParticleColorState
{
    bool  enabled           = false;     ///< Whether particle colour replacement is active.
    float colors[3][3][3]   = {};        ///< Colour values [set 0..2 for IDs 11,12,13][phase 0..2][r/g/b].
    bool  hasSet[3]         = {};        ///< Which colour IDs (11, 12, 13) are present in the model.
};

/// @brief Per-frame context for the viewport options panel.
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
