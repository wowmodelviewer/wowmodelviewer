#include "ItemBrowserPanel.h"

#include "imgui.h"
#include "database.h"

#include <format>

namespace ItemBrowserPanel
{

namespace
{

ImVec4 getQualityColor(int quality)
{
    switch (quality)
    {
    case 0:  return ImVec4(0.616f, 0.616f, 0.616f, 1.0f); // Poor (gray)
    case 1:  return ImVec4(1.0f,   1.0f,   1.0f,   1.0f); // Common (white)
    case 2:  return ImVec4(0.118f, 1.0f,   0.0f,   1.0f); // Uncommon (green)
    case 3:  return ImVec4(0.0f,   0.439f, 0.867f, 1.0f); // Rare (blue)
    case 4:  return ImVec4(0.639f, 0.208f, 0.933f, 1.0f); // Epic (purple)
    case 5:  return ImVec4(1.0f,   0.502f, 0.0f,   1.0f); // Legendary (orange)
    case 6:
    case 7:  return ImVec4(0.898f, 0.8f,   0.502f, 1.0f); // Artifact/Heirloom (gold)
    default: return ImVec4(1.0f,   1.0f,   1.0f,   1.0f);
    }
}

} // anonymous namespace

void draw(DrawContext& ctx)
{
    if (!ctx.isWoWLoaded || !ctx.isDBReady)
    {
        ImGui::TextDisabled("Game not loaded.");
        return;
    }

    ImGui::Text("Search:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::InputText("##itemBrowseSearch", ctx.itemBrowseSearchBuf, ctx.itemBrowseSearchBufSize,
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (ctx.itemBrowseFilterDirty) *ctx.itemBrowseFilterDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply##itembrowse", ImVec2(-1, 0)))
    {
        if (ctx.itemBrowseFilterDirty) *ctx.itemBrowseFilterDirty = true;
    }

    if (ctx.itemBrowseFilterDirty && *ctx.itemBrowseFilterDirty && ctx.rebuildItemBrowseFilter)
        ctx.rebuildItemBrowseFilter();

    if (ctx.itemBrowseFiltered)
        ImGui::Text("%d items", static_cast<int>(ctx.itemBrowseFiltered->size()));
    ImGui::Separator();

    ImGui::BeginChild("##ItemBrowseList", ImVec2(0, 0), ImGuiChildFlags_None);
    if (ctx.itemBrowseFiltered && ctx.items)
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ctx.itemBrowseFiltered->size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& item = ctx.items->items[(*ctx.itemBrowseFiltered)[i]];
                ImGui::PushID(static_cast<int>((*ctx.itemBrowseFiltered)[i]));
                ImVec4 qcol = getQualityColor(item.quality);
                ImGui::PushStyleColor(ImGuiCol_Text, qcol);
                std::string label = std::format("{} ({})", item.name, item.id);
                if (ImGui::Selectable(label.c_str()))
                {
                    if (ctx.loadItemModel)
                        ctx.loadItemModel(static_cast<unsigned int>(item.id));
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

} // namespace ItemBrowserPanel
