#include "NpcBrowserPanel.h"

#include <cassert>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "database.h"

#include <format>

namespace NpcBrowserPanel
{

void draw(DrawContext& ctx)
{
    assert(ctx.npcs && "DrawContext::npcs must not be null");
    assert(ctx.npcFiltered && "DrawContext::npcFiltered must not be null");
    assert(ctx.npcSearchBuf && "DrawContext::npcSearchBuf must not be null");

    if (!ctx.isWoWLoaded || !ctx.isDBReady)
    {
        ImGui::TextDisabled("Game not loaded.");
        return;
    }

    ImGui::Text("Search:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::InputText("##npcSearch", ctx.npcSearchBuf,
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (ctx.npcFilterDirty) *ctx.npcFilterDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply##npc", ImVec2(-1, 0)))
    {
        if (ctx.npcFilterDirty) *ctx.npcFilterDirty = true;
    }

    if (ctx.npcFilterDirty && *ctx.npcFilterDirty && ctx.rebuildNpcFilter)
        ctx.rebuildNpcFilter();

    if (ctx.npcFiltered)
        ImGui::Text("%d NPCs", static_cast<int>(ctx.npcFiltered->size()));
    ImGui::Separator();

    ImGui::BeginChild("##NpcList", ImVec2(0, 0), ImGuiChildFlags_None);
    if (ctx.npcFiltered && ctx.npcs)
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ctx.npcFiltered->size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& npc = (*ctx.npcs)[(*ctx.npcFiltered)[i]];
                ImGui::PushID(static_cast<int>((*ctx.npcFiltered)[i]));
                std::string label = std::format("{} (ID:{} Type:{})", npc.name, npc.id, npc.type);
                if (ImGui::Selectable(label.c_str()))
                {
                    if (ctx.loadNPC)
                        ctx.loadNPC(static_cast<unsigned int>(npc.id));
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

} // namespace NpcBrowserPanel
