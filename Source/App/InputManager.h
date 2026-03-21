#pragma once

// ---- Input Manager --------------------------------------------------------
// Owns a set of InputBindings and resolves them against ImGui IO state each
// frame to produce an InputState.  The bindings are configurable data;
// loadDefaults() reproduces the original hardcoded viewer controls.

#include "InputAction.h"

#include <vector>

class InputManager
{
public:
    InputManager();

    /// Populate the binding table with default viewer controls.
    void loadDefaults();

    /// Read current ImGui IO state and resolve all bindings into m_state.
    /// Call once per frame, after ImGui::NewFrame().
    void update();

    /// Resolved state from the most recent update().
    const InputState& state() const noexcept { return m_state; }

    /// Mutable access to bindings (for future settings UI / serialisation).
    std::vector<InputBinding>& bindings() noexcept { return m_bindings; }
    const std::vector<InputBinding>& bindings() const noexcept { return m_bindings; }

private:
    std::vector<InputBinding> m_bindings;
    InputState m_state;
};
