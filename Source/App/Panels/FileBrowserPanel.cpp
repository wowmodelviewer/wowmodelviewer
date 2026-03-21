#include "FileBrowserPanel.h"

#include "imgui.h"

#include <string>
#include <map>
#include <deque>
#include <format>
#include <algorithm>
#include <functional>

#include "Logger.h"
#include "Game.h"
#include "WoWFolder.h"
#include "string_utils.h"

// ---- Internal types -------------------------------------------------------
namespace
{

struct FileBrowserNode
{
    std::string                              name;
    GameFile*                                file = nullptr;
    std::map<std::string, FileBrowserNode*>  children;
};

// Arena allocator: all tree nodes live in a deque and are freed in bulk.
// std::deque never moves existing elements on push_back, so FileBrowserNode*
// pointers stored in children maps remain valid as the pool grows.
std::deque<FileBrowserNode> s_nodePool;

FileBrowserNode* allocNode()
{
    s_nodePool.emplace_back();
    return &s_nodePool.back();
}

void freeNodePool()
{
    s_nodePool.clear();
}

// ---- Filter data ----------------------------------------------------------
const char* s_filterLabels[] = {
    "Models (*.m2)",
    "WMOs (*.wmo)",
    "ADTs (*.adt)",
    "Sounds (*.wav)",
    "OGGs (*.ogg)",
    "MP3s (*.mp3)",
    "Images (*.blp)",
    "Shaders (*.bls)",
    "DBCs (*.dbc)",
    "DB2s (*.db2)",
    "LUAs (*.lua)",
    "XMLs (*.xml)",
    "SKINs (*.skin)"
};

const char* s_filterExtensions[] = {
    "m2", "wmo", "adt", "wav", "ogg", "mp3",
    "blp", "bls", "dbc", "db2", "lua", "xml", "skin"
};

constexpr int s_filterCount = sizeof(s_filterLabels) / sizeof(s_filterLabels[0]);

// ---- Panel state ----------------------------------------------------------
int              s_filterMode     = 0;
char             s_searchBuf[256] = {};
FileBrowserNode* s_fileTreeRoot   = nullptr;
bool             s_fileTreeDirty  = true;
int              s_fileTreeFileCount = 0;

// ---- Tree construction ----------------------------------------------------
void rebuildFileTree()
{
    freeNodePool();
    s_fileTreeRoot = allocNode();
    s_fileTreeRoot->name = "Root";

    std::string search = core::toLower(std::string(s_searchBuf));
    auto s = search.find_first_not_of(" \t\r\n");
    auto e = search.find_last_not_of(" \t\r\n");
    search = (s == std::string::npos) ? "" : search.substr(s, e - s + 1);

    const std::string ext = std::string(".") + s_filterExtensions[s_filterMode];

    s_fileTreeFileCount = 0;
    for (auto* gf : GAMEDIRECTORY)
    {
        const auto& fname = gf->fullname();
        if (!core::endsWithIgnoreCase(fname, ext))
            continue;
        if (!search.empty() && !core::containsIgnoreCase(fname, search))
            continue;

        ++s_fileTreeFileCount;

        std::string displayName = std::format("{} [{}]", fname, gf->fileDataId());
        std::transform(displayName.begin(), displayName.end(), displayName.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::replace(displayName.begin(), displayName.end(), '/', '\\');
        if (!displayName.empty())
            displayName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(displayName[0])));

        auto parts = core::split(displayName, '\\');
        FileBrowserNode* cur = s_fileTreeRoot;

        for (int i = 0; i < static_cast<int>(parts.size()) - 1; ++i)
        {
            auto it = cur->children.find(parts[i]);
            if (it == cur->children.end())
            {
                auto* child = allocNode();
                child->name = parts[i];
                cur->children[parts[i]] = child;
                cur = child;
            }
            else
            {
                cur = it->second;
            }
        }

        auto* leaf = allocNode();
        leaf->name = parts.back();
        leaf->file = gf;
        cur->children[parts.back()] = leaf;
    }

    s_fileTreeDirty = false;
    LOG_INFO << "File tree rebuilt: " << s_fileTreeFileCount << " files matching filter.";
}

} // anonymous namespace

// ---- Public API -----------------------------------------------------------
namespace FileBrowserPanel
{

GameFile* draw(bool& visible, const LoadState& load)
{
    GameFile* selected = nullptr;

    if (ImGui::Begin("File Browser", &visible))
    {
        if (!load.isLoaded)
        {
            if (load.inProgress)
            {
                ImGui::Text("Loading...");
                ImGui::ProgressBar(load.progress, ImVec2(-1, 0));
                ImGui::TextWrapped("%s", load.statusText);
            }
            else
            {
                if (load.statusText && load.statusText[0] != '\0')
                    ImGui::TextWrapped("%s", load.statusText);
                else
                    ImGui::TextWrapped("Game not loaded. Use Settings panel to set the WoW path and click Load WoW.");
            }
        }
        else
        {
            // ---- Filter options ----
            ImGui::Text("Filter:");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::Combo("##filter", &s_filterMode, s_filterLabels, s_filterCount))
                s_fileTreeDirty = true;

            ImGui::Text("Search:");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
            if (ImGui::InputText("##search", s_searchBuf, sizeof(s_searchBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                s_fileTreeDirty = true;
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(-1, 0)))
                s_fileTreeDirty = true;

            if (s_fileTreeDirty)
                rebuildFileTree();

            ImGui::Separator();
            ImGui::Text("Files: %d", s_fileTreeFileCount);
            ImGui::Separator();

            // ---- File tree ----
            ImGui::BeginChild("FileTree", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            if (s_fileTreeRoot)
            {
                std::function<void(FileBrowserNode*)> drawNode = [&](FileBrowserNode* node)
                {
                    for (auto& [name, child] : node->children)
                    {
                        if (child->file)
                        {
                            ImGuiTreeNodeFlags leafFlags =
                                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                ImGuiTreeNodeFlags_SpanAvailWidth;
                            ImGui::TreeNodeEx(child->name.c_str(), leafFlags);
                            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                                selected = child->file;
                        }
                        else
                        {
                            bool open = ImGui::TreeNodeEx(child->name.c_str(),
                                ImGuiTreeNodeFlags_SpanAvailWidth);
                            if (open)
                            {
                                drawNode(child);
                                ImGui::TreePop();
                            }
                        }
                    }
                };
                drawNode(s_fileTreeRoot);
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();

    return selected;
}

void markDirty()
{
    s_fileTreeDirty = true;
}

void shutdown()
{
    freeNodePool();
    s_fileTreeRoot = nullptr;
}

} // namespace FileBrowserPanel
