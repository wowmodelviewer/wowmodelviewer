#pragma once

class GameFile;

namespace FileBrowserPanel
{

/// Information about the game-loading progress, passed in by the caller
/// so the panel can show status / progress without owning the load state.
struct LoadState
{
    bool  isLoaded    = false;
    bool  inProgress  = false;
    float progress    = 0.0f;   // 0..1
    const char* statusText = "";
};

/// Draw the File Browser ImGui panel.
/// @param load     Current game-loading state.
/// @return         The GameFile the user clicked, or nullptr if nothing was
///                 selected this frame.
GameFile* draw(const LoadState& load);

/// Mark the file tree as dirty so it will be rebuilt on the next draw().
void markDirty();

/// Free all internal allocations (call at application shutdown).
void shutdown();

} // namespace FileBrowserPanel
