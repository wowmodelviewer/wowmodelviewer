#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "AnimationPanel.h"

class GameFile;
class Renderer;
class WoWModel;
class Attachment;
class OrbitCamera;
struct ViewportFBO;
struct AppSettings;

namespace CharacterViewerPanel
{

/// Customization option visible in the left column.
/// Populated by the caller (initCharacterControl) and mutated by the panel.
struct CustomizationOption
{
    unsigned int optionID = 0;
    std::string name;
    std::vector<unsigned int> choiceIDs;
    std::vector<std::string> choiceNames;
    int selectedIndex = 0;
};

/// Animation entry — shared definition from AnimationPanel.
using AnimEntry = AnimationPanel::AnimEntry;

/// Per-frame context passed by the caller so the panel never touches globals.
struct DrawContext
{
    bool           isWoWLoaded  = false;
    bool           isDBReady    = false;
    bool           isChar       = false;

    // References to shared mutable state owned by main
    std::vector<CustomizationOption>* customizationOptions = nullptr;
    std::vector<AnimEntry>*           animEntries          = nullptr;
    int*                              selectedAnimCombo    = nullptr;

    // Rendering resources
    Renderer*  renderer = nullptr;
    ViewportFBO*   fbo    = nullptr;
    OrbitCamera*   camera = nullptr;
    Attachment*    root   = nullptr;
    float          fov    = 0.785f;
    glm::vec3      bgColor{0.22f};
    bool           drawGrid = false;

    // Callbacks into main logic
    std::function<WoWModel*()>       getLoadedModel;
    std::function<void(GameFile*)>   loadModel;
    std::function<void()>            handleViewportInput;
};

/// Draw the Character Viewer panel contents (call between ImGui::Begin / End).
void draw(const DrawContext& ctx);

} // namespace CharacterViewerPanel
