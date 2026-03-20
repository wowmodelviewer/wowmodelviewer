#include "ItemSetsPanel.h"

#include "imgui.h"

#include <format>

namespace ItemSetsPanel
{

void draw(DrawContext& ctx)
{
    WoWModel* cModel = ctx.getLoadedModel ? ctx.getLoadedModel() : nullptr;
    if (!cModel || !ctx.isChar)
    {
        ImGui::TextDisabled("Load a character model first.");
        return;
    }

    // ---- Item Sets ----
    if (ctx.buildItemSets)
        ctx.buildItemSets(); // lazy init on first frame

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::InputText("##itemSetSearch", ctx.itemSetSearchBuf, ctx.itemSetSearchBufSize,
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (ctx.itemSetFilterDirty) *ctx.itemSetFilterDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply##itemset", ImVec2(-1, 0)))
    {
        if (ctx.itemSetFilterDirty) *ctx.itemSetFilterDirty = true;
    }

    if (ctx.itemSetFilterDirty && *ctx.itemSetFilterDirty && ctx.rebuildItemSetFilter)
        ctx.rebuildItemSetFilter();

    if (ctx.itemSetFiltered)
        ImGui::Text("%d sets", static_cast<int>(ctx.itemSetFiltered->size()));
    ImGui::Separator();

    ImGui::BeginChild("##ItemSetList", ImVec2(0, 200), ImGuiChildFlags_Borders);
    if (ctx.itemSetFiltered && ctx.itemSets)
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ctx.itemSetFiltered->size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& setEntry = (*ctx.itemSets)[(*ctx.itemSetFiltered)[i]];
                ImGui::PushID(setEntry.id);
                std::string label = std::format("{} (ID:{})", setEntry.name, setEntry.id);
                if (ImGui::Selectable(label.c_str()))
                {
                    if (ctx.applyItemSet)
                        ctx.applyItemSet(cModel, setEntry.id);
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();

    // ---- Start Outfits ----
    ImGui::SeparatorText("Start Outfits");

    if (ctx.startOutfitsBuilt && !*ctx.startOutfitsBuilt && ctx.buildStartOutfits)
        ctx.buildStartOutfits(cModel);

    if (ctx.startOutfits && ctx.startOutfits->empty())
    {
        ImGui::TextDisabled("No start outfits available for this race/sex.");
        return;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::InputText("##startOutfitSearch", ctx.startOutfitSearchBuf, ctx.startOutfitSearchBufSize,
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (ctx.startOutfitFilterDirty) *ctx.startOutfitFilterDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply##startoutfit", ImVec2(-1, 0)))
    {
        if (ctx.startOutfitFilterDirty) *ctx.startOutfitFilterDirty = true;
    }

    if (ctx.startOutfitFilterDirty && *ctx.startOutfitFilterDirty && ctx.rebuildStartOutfitFilter)
        ctx.rebuildStartOutfitFilter();

    if (ctx.startOutfitFiltered)
        ImGui::Text("%d classes", static_cast<int>(ctx.startOutfitFiltered->size()));
    ImGui::Separator();

    ImGui::BeginChild("##StartOutfitList", ImVec2(0, 150), ImGuiChildFlags_Borders);
    if (ctx.startOutfitFiltered && ctx.startOutfits)
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(ctx.startOutfitFiltered->size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& entry = (*ctx.startOutfits)[(*ctx.startOutfitFiltered)[i]];
                ImGui::PushID(entry.id);
                std::string label = std::format("{} (ID:{})", entry.name, entry.id);
                if (ImGui::Selectable(label.c_str()))
                {
                    if (ctx.applyStartOutfit)
                        ctx.applyStartOutfit(cModel, entry.id);
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

} // namespace ItemSetsPanel
