#include "LogPanel.h"

#include <cassert>

#include "imgui.h"

#include <fstream>
#include <string>

namespace
{

void reloadLogFile(std::vector<std::string>& lines, bool& needsReload)
{
    lines.clear();
    std::ifstream file("userSettings/log_imgui.txt");
    if (!file.is_open())
        return;
    std::string line;
    while (std::getline(file, line))
        lines.push_back(line);
    needsReload = false;
}

} // anonymous namespace

void LogPanel::draw(DrawContext& ctx)
{
    assert(ctx.logLines && "DrawContext::logLines must not be null");
    assert(ctx.logAutoScroll && "DrawContext::logAutoScroll must not be null");
    assert(ctx.logNeedsReload && "DrawContext::logNeedsReload must not be null");

    if (*ctx.logNeedsReload)
        reloadLogFile(*ctx.logLines, *ctx.logNeedsReload);

    if (ImGui::Button("Reload"))
        reloadLogFile(*ctx.logLines, *ctx.logNeedsReload);
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        ctx.logLines->clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", ctx.logAutoScroll);
    ImGui::SameLine();
    ImGui::TextDisabled("%d lines", static_cast<int>(ctx.logLines->size()));

    ImGui::Separator();
    ImGui::BeginChild("##LogScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(ctx.logLines->size()));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const auto& line = (*ctx.logLines)[i];
            if (line.find("ERROR") != std::string::npos)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            else if (line.find("WARNING") != std::string::npos)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (*ctx.logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}
