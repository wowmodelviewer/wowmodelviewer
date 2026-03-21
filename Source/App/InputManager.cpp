#include "InputManager.h"

#include "imgui.h"

InputManager::InputManager()
{
    loadDefaults();
}

// ---------------------------------------------------------------------------
// Default bindings — reproduces the original hardcoded viewer controls
// ---------------------------------------------------------------------------

void InputManager::loadDefaults()
{
    m_bindings.clear();
    m_bindings.reserve(14);

    // ---- Mouse bindings ---------------------------------------------------

    // Left drag -> orbit (yaw + pitch)
    m_bindings.push_back({ViewportAction::Orbit, TriggerType::MouseDrag,
                          ImGuiMouseButton_Left, ImGuiKey_None,
                          -0.25f, -0.25f, 0.0f, 0.1f});

    // Right drag -> pan (lateral + vertical)
    m_bindings.push_back({ViewportAction::Pan, TriggerType::MouseDrag,
                          ImGuiMouseButton_Right, ImGuiKey_None,
                          -(0.25f * 0.025f), (0.25f * 0.025f), 0.0f, 0.1f});

    // Middle drag -> zoom (Y-axis only)
    m_bindings.push_back({ViewportAction::Zoom, TriggerType::MouseDrag,
                          ImGuiMouseButton_Middle, ImGuiKey_None,
                          0.0f, (0.25f / 10.0f), 0.0f, 0.1f});

    // Scroll wheel -> zoom
    m_bindings.push_back({ViewportAction::Zoom, TriggerType::MouseScroll,
                          ImGuiMouseButton_Left, ImGuiKey_None,
                          -0.5f, 0.0f, 0.0f, 0.1f});

    // ---- Numpad orbit -----------------------------------------------------

    // Numpad 4 -> yaw left
    m_bindings.push_back({ViewportAction::OrbitYaw, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad4,
                          0.0f, 0.0f, 1.0f, 1.0f});

    // Numpad 6 -> yaw right
    m_bindings.push_back({ViewportAction::OrbitYaw, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad6,
                          0.0f, 0.0f, -1.0f, 1.0f});

    // Numpad 8 -> pitch up
    m_bindings.push_back({ViewportAction::OrbitPitch, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad8,
                          0.0f, 0.0f, 1.0f, 1.0f});

    // Numpad 2 -> pitch down
    m_bindings.push_back({ViewportAction::OrbitPitch, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad2,
                          0.0f, 0.0f, -1.0f, 1.0f});

    // ---- Numpad pan -------------------------------------------------------

    // Numpad 7 -> pan up (world Z+)
    m_bindings.push_back({ViewportAction::PanVertical, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad7,
                          0.0f, 0.0f, 0.2f, 1.0f});

    // Numpad 9 -> pan down (world Z-)
    m_bindings.push_back({ViewportAction::PanVertical, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad9,
                          0.0f, 0.0f, -0.2f, 1.0f});

    // Numpad 1 -> pan left (camera right -)
    m_bindings.push_back({ViewportAction::PanLateral, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad1,
                          0.0f, 0.0f, -0.2f, 1.0f});

    // Numpad 3 -> pan right (camera right +)
    m_bindings.push_back({ViewportAction::PanLateral, TriggerType::KeyHold,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad3,
                          0.0f, 0.0f, 0.2f, 1.0f});

    // ---- Reset ------------------------------------------------------------

    // Numpad 5 -> reset camera
    m_bindings.push_back({ViewportAction::ResetCamera, TriggerType::KeyPress,
                          ImGuiMouseButton_Left, ImGuiKey_Keypad5,
                          0.0f, 0.0f, 0.0f, 1.0f});
}

// ---------------------------------------------------------------------------
// Per-frame update — resolve all bindings against ImGui IO
// ---------------------------------------------------------------------------

void InputManager::update()
{
    m_state = {};

    const ImGuiIO& io = ImGui::GetIO();

    for (const auto& b : m_bindings)
    {
        const float shiftMul = (io.KeyShift && b.shiftScale != 1.0f)
                                ? b.shiftScale : 1.0f;

        switch (b.trigger)
        {
        case TriggerType::MouseDrag:
        {
            if (!ImGui::IsMouseDragging(b.mouseButton))
                break;

            const float dx = io.MouseDelta.x * b.scaleX * shiftMul;
            const float dy = io.MouseDelta.y * b.scaleY * shiftMul;

            switch (b.action)
            {
            case ViewportAction::Orbit:
                m_state.orbitYaw   += dx;
                m_state.orbitPitch += dy;
                break;
            case ViewportAction::Pan:
                m_state.panX += dx;
                m_state.panZ += dy;
                break;
            case ViewportAction::Zoom:
                m_state.zoom += dy;
                break;
            default:
                break;
            }
            break;
        }

        case TriggerType::MouseScroll:
        {
            if (io.MouseWheel == 0.0f)
                break;

            const float scroll = io.MouseWheel * b.scaleX * shiftMul;

            if (b.action == ViewportAction::Zoom)
                m_state.zoom += scroll;
            break;
        }

        case TriggerType::KeyHold:
        {
            if (!ImGui::IsKeyDown(b.key))
                break;

            const float delta = b.keyDelta;

            switch (b.action)
            {
            case ViewportAction::OrbitYaw:    m_state.orbitYaw   += delta; break;
            case ViewportAction::OrbitPitch:  m_state.orbitPitch += delta; break;
            case ViewportAction::PanLateral:  m_state.panX       += delta; break;
            case ViewportAction::PanVertical: m_state.panZ       += delta; break;
            case ViewportAction::Zoom:        m_state.zoom       += delta; break;
            default: break;
            }
            break;
        }

        case TriggerType::KeyPress:
        {
            if (!ImGui::IsKeyPressed(b.key))
                break;

            if (b.action == ViewportAction::ResetCamera)
                m_state.resetCamera = true;
            break;
        }
        }
    }
}
