#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

class GameFile;
class WoWModel;

namespace AnimationPanel
{

/// Reusable animation entry — matches CharacterViewerPanel::AnimEntry.
struct AnimEntry
{
    std::string label;
    int animIndex = 0;
};

/// Skin / texture variant entry used by the skin selector and by the model
/// loader when building the creature skin list.
struct SkinEntry
{
    std::string label;
    GameFile* tex[3] = {nullptr, nullptr, nullptr};
    int base = 0;
    size_t count = 0;
    std::set<int> creatureGeosetData;
};

/// Per-frame context passed by the caller so the panel never touches globals.
struct DrawContext
{
    // Model accessor
    std::function<WoWModel*()> getLoadedModel;

    // Animation state (pointers into AppState)
    std::vector<AnimEntry>* animEntries       = nullptr;
    int*                    selectedAnimCombo  = nullptr;
    float*                  animSpeed          = nullptr;
    int*                    loopCount          = nullptr;
    bool*                   lockAnims          = nullptr;
    int*                    selectedSecondaryAnim = nullptr;
    int*                    selectedMouthAnim  = nullptr;
    float*                  mouthSpeed         = nullptr;

    // Skin state (pointers into AppState)
    std::vector<SkinEntry>* skinEntries = nullptr;
    int*                    selectedSkin = nullptr;
    int                     blpSkin[3]  = {-1, -1, -1}; // copied in, copied out
    std::function<void(WoWModel*, int)> applySkin;
};

/// Draw the Animation panel contents (call between ImGui::Begin / End).
void draw(DrawContext& ctx);

} // namespace AnimationPanel
