// ExportPanel.cpp  –  Export panel (format, path, animation selection, export)
#include "ExportPanel.h"
#include "imgui.h"
#include "ExporterPlugin.h"
#include "WoWModel.h"
#include "Logger.h"
#include "string_utils.h"

#include <algorithm>
#include <format>

namespace
{

std::string wstringToString(const std::wstring& ws)
{
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws)
        s.push_back(static_cast<char>(c & 0x7F));
    return s;
}

std::wstring stringToWstring(const std::string& s)
{
    std::wstring ws;
    ws.reserve(s.size());
    for (char c : s)
        ws.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return ws;
}

void doExport(ExportPanel::DrawContext& ctx)
{
    WoWModel* model = ctx.getLoadedModel ? ctx.getLoadedModel() : nullptr;
    if (!model)
    {
        *ctx.exportStatus = "No model loaded.";
        return;
    }

    if (*ctx.selectedExporter < 0 ||
        *ctx.selectedExporter >= static_cast<int>(ctx.exporters->size()))
    {
        *ctx.exportStatus = "Invalid exporter selection.";
        return;
    }

    ExporterPlugin* exporter = (*ctx.exporters)[*ctx.selectedExporter].get();

    // Build file path with appropriate extension
    std::string pathStr{ctx.exportPath};
    std::wstring filter = exporter->fileSaveFilter();
    std::string ext;
    {
        std::string f = wstringToString(filter);
        auto pos = f.find("*.");
        if (pos != std::string::npos)
        {
            auto end = f.find_first_of(";|)", pos);
            ext = f.substr(pos + 1,
                           (end == std::string::npos) ? std::string::npos : end - pos - 1);
        }
    }

    if (!ext.empty() && !core::endsWithIgnoreCase(pathStr, ext))
        pathStr += ext;

    // Gather selected animation indices
    if (exporter->canExportAnimation())
    {
        std::vector<int> animsToExport;
        for (size_t i = 0; i < ctx.exportAnimChecked->size() && i < model->anims.size(); ++i)
        {
            if ((*ctx.exportAnimChecked)[i])
                animsToExport.push_back(model->anims[i].Index);
        }
        exporter->setAnimationsToExport(animsToExport);
    }

    std::wstring wpath = stringToWstring(pathStr);
    LOG_INFO << "Exporting model to: " << pathStr;

    if (exporter->exportModel(model, wpath))
    {
        *ctx.exportStatus = "Export successful: " + pathStr;
        LOG_INFO << "Export complete: " << pathStr;
    }
    else
    {
        *ctx.exportStatus = "Export failed: " + pathStr;
        LOG_ERROR << "Export failed: " << pathStr;
    }
}

} // anonymous namespace

namespace ExportPanel
{

void draw(DrawContext& ctx)
{
    WoWModel* eModel = ctx.getLoadedModel ? ctx.getLoadedModel() : nullptr;
    if (eModel)
    {
        // ---- Format selector ----
        ImGui::SeparatorText("Format");
        if (!ctx.exporters->empty())
        {
            for (int i = 0; i < static_cast<int>(ctx.exporters->size()); ++i)
            {
                std::string label = wstringToString((*ctx.exporters)[i]->menuLabel());
                ImGui::RadioButton(label.c_str(), ctx.selectedExporter, i);
                if (i < static_cast<int>(ctx.exporters->size()) - 1)
                    ImGui::SameLine();
            }
        }

        // ---- Output path ----
        ImGui::SeparatorText("Output");
        ImGui::Text("File Path:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##exportPath", ctx.exportPath,
                         static_cast<int>(ctx.exportPathSize));

        // ---- Animation selection (only for exporters that support it) ----
        bool canAnim = (*ctx.selectedExporter >= 0 &&
                        *ctx.selectedExporter < static_cast<int>(ctx.exporters->size()) &&
                        (*ctx.exporters)[*ctx.selectedExporter]->canExportAnimation());

        if (canAnim && !ctx.animEntries->empty())
        {
            ImGui::SeparatorText("Animations");

            if (ctx.exportAnimChecked->size() != ctx.animEntries->size())
                ctx.exportAnimChecked->assign(ctx.animEntries->size(), 1);

            if (ImGui::Button("Select All"))
                std::fill(ctx.exportAnimChecked->begin(),
                          ctx.exportAnimChecked->end(), static_cast<char>(1));
            ImGui::SameLine();
            if (ImGui::Button("Select None"))
                std::fill(ctx.exportAnimChecked->begin(),
                          ctx.exportAnimChecked->end(), static_cast<char>(0));

            int checkedCount = 0;
            for (char b : *ctx.exportAnimChecked) if (b) ++checkedCount;
            ImGui::Text("%d / %d selected", checkedCount,
                        static_cast<int>(ctx.animEntries->size()));

            ImGui::BeginChild("##AnimExportList", ImVec2(0, 200), ImGuiChildFlags_Borders);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(ctx.animEntries->size()));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    ImGui::PushID(i);
                    bool checked = (*ctx.exportAnimChecked)[i] != 0;
                    if (ImGui::Checkbox((*ctx.animEntries)[i].label.c_str(), &checked))
                        (*ctx.exportAnimChecked)[i] = checked ? 1 : 0;
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        else if (canAnim)
        {
            ImGui::SeparatorText("Animations");
            ImGui::TextDisabled("No animations on current model.");
        }

        // ---- Export buttons ----
        ImGui::Separator();
        if (ImGui::Button("Export", ImVec2(-1, 0)))
            doExport(ctx);

        if (canAnim && !ctx.animEntries->empty() && *ctx.selectedAnimCombo >= 0)
        {
            if (ImGui::Button("Export Current Anim Only", ImVec2(-1, 0)))
            {
                std::vector<char> saved = *ctx.exportAnimChecked;
                ctx.exportAnimChecked->assign(ctx.animEntries->size(), 0);
                if (*ctx.selectedAnimCombo < static_cast<int>(ctx.exportAnimChecked->size()))
                    (*ctx.exportAnimChecked)[*ctx.selectedAnimCombo] = 1;
                doExport(ctx);
                *ctx.exportAnimChecked = std::move(saved);
            }
        }

        // ---- Status ----
        if (!ctx.exportStatus->empty())
        {
            bool isError = ctx.exportStatus->find("failed") != std::string::npos ||
                           ctx.exportStatus->find("No ") != std::string::npos ||
                           ctx.exportStatus->find("Invalid") != std::string::npos;
            if (isError)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ctx.exportStatus->c_str());
            else
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", ctx.exportStatus->c_str());
        }
    }
    else
    {
        ImGui::TextDisabled("No model loaded.");
    }
}

} // namespace ExportPanel
