#include "MountsPanel.h"

#include <cassert>

#include "imgui.h"
#include "imgui_stdlib.h"

#include <format>

namespace MountsPanel
{

void draw(DrawContext& ctx)
{
    assert(ctx.mountList && "DrawContext::mountList must not be null");
    assert(ctx.mountFiltered && "DrawContext::mountFiltered must not be null");
    assert(ctx.mountSearchBuf && "DrawContext::mountSearchBuf must not be null");
    assert(ctx.mountTab && "DrawContext::mountTab must not be null");

    WoWModel* cModel = ctx.getLoadedModel ? ctx.getLoadedModel() : nullptr;
    if (cModel && ctx.isChar)
    {
        if (ctx.buildMountList)
            ctx.buildMountList(); // lazy init on first frame

        if (ctx.isMounted)
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Mounted");
            if (ImGui::Button("Dismount", ImVec2(-1, 0)))
            {
                if (ctx.dismountCharacter)
                    ctx.dismountCharacter();
            }
        }
        else
        {
            if (ImGui::BeginTabBar("##MountTabs"))
            {
                int prevTab = ctx.mountTab ? *ctx.mountTab : 0;

                if (ImGui::BeginTabItem("Player Mounts"))
                {
                    if (ctx.mountTab) *ctx.mountTab = 0;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Creature Models"))
                {
                    if (ctx.mountTab) *ctx.mountTab = 1;
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();

                if (ctx.mountTab && *ctx.mountTab != prevTab)
                {
                    if (ctx.mountFilterDirty) *ctx.mountFilterDirty = true;
                    if (ctx.mountSearchBuf) ctx.mountSearchBuf->clear();
                }
            }

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
            if (ImGui::InputText("##mountSearch", ctx.mountSearchBuf,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                if (ctx.mountFilterDirty) *ctx.mountFilterDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply##mount", ImVec2(-1, 0)))
            {
                if (ctx.mountFilterDirty) *ctx.mountFilterDirty = true;
            }

            if (ctx.mountFilterDirty && *ctx.mountFilterDirty && ctx.rebuildMountFilter)
                ctx.rebuildMountFilter();

            if (ctx.mountFiltered)
                ImGui::Text("%d entries", static_cast<int>(ctx.mountFiltered->size()));
            ImGui::Separator();

            ImGui::BeginChild("##MountList", ImVec2(0, 0), ImGuiChildFlags_Borders);
            if (ctx.mountFiltered)
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(ctx.mountFiltered->size()));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        size_t idx = (*ctx.mountFiltered)[i];
                        ImGui::PushID(static_cast<int>(idx));

                        int tab = ctx.mountTab ? *ctx.mountTab : 0;
                        if (tab == 0 && ctx.mountList)
                        {
                            const auto& me = (*ctx.mountList)[idx];
                            std::string label = std::format("{} (DisplayID:{})", me.name, me.displayID);
                            if (ImGui::Selectable(label.c_str()))
                            {
                                if (ctx.mountCharacter)
                                    ctx.mountCharacter(me.displayID, nullptr);
                            }
                        }
                        else if (ctx.creatureModelNames && ctx.creatureModels)
                        {
                            if (ImGui::Selectable((*ctx.creatureModelNames)[idx].c_str()))
                            {
                                if (ctx.mountCharacter)
                                    ctx.mountCharacter(-1, (*ctx.creatureModels)[idx]);
                            }
                        }

                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();
        }
    }
    else
    {
        ImGui::TextDisabled("Load a character model first.");
    }
}

} // namespace MountsPanel
