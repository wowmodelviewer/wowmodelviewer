#include "PresetsPanel.h"

#include "imgui.h"

void PresetsPanel::draw(DrawContext& ctx)
{
    ImGui::SeparatorText("Character Preset");
    ImGui::Text("File:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##presetPath", ctx.presetPath, ctx.presetPathSize);

    {
        bool canSave = ctx.isChar && ctx.hasModel;
        if (!canSave) ImGui::BeginDisabled();

        if (ImGui::Button("Save Preset", ImVec2(-1, 0)))
            ctx.savePreset(ctx.presetPath);

        if (!canSave) ImGui::EndDisabled();
    }

    {
        bool canLoad = ctx.isChar && ctx.hasModel;
        if (!canLoad) ImGui::BeginDisabled();

        if (ImGui::Button("Load Preset", ImVec2(-1, 0)))
            ctx.loadPreset(ctx.presetPath);

        if (!canLoad) ImGui::EndDisabled();
    }

    if (!ctx.presetStatus->empty())
    {
        bool isError = ctx.presetStatus->find("not found") != std::string::npos ||
                       ctx.presetStatus->find("No ") != std::string::npos;
        if (isError)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ctx.presetStatus->c_str());
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", ctx.presetStatus->c_str());
    }

    if (!ctx.isChar)
        ImGui::TextDisabled("Load a character model first.");
}
