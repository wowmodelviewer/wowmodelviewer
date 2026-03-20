#include "ViewportOptionsPanel.h"

#include "imgui.h"
#include "SceneRenderer.h"
#include "OrbitCamera.h"
#include "WoWModel.h"

#include <format>

#include <glm/glm.hpp>

namespace
{

// ---- Background colour palette (constant data) ---------------------------
constexpr glm::vec3 g_bgPalette[] = {
    {0.22f, 0.22f, 0.22f},   // Dark gray (default)
    {0.0f,  0.0f,  0.0f},    // Black
    {1.0f,  1.0f,  1.0f},    // White
    {0.278f,0.373f,0.475f},   // Steel blue
    {0.15f, 0.30f, 0.15f},   // Forest green
    {0.35f, 0.15f, 0.15f},   // Dark red
    {0.0f,  0.47f, 0.84f},   // WoW blue
    {0.10f, 0.10f, 0.18f},   // Midnight
};
constexpr int g_bgPaletteCount = sizeof(g_bgPalette) / sizeof(g_bgPalette[0]);

// ---- Particle color helper -----------------------------------------------
void applyParticleColors(WoWModel* model,
                         ViewportOptionsPanel::ParticleColorState& pcrState)
{
    if (!model) return;

    model->particleColorReplacements.clear();
    for (int s = 0; s < 3; ++s)
    {
        std::vector<glm::vec4> pcs;
        for (int p = 0; p < 3; ++p)
        {
            pcs.push_back(glm::vec4(
                pcrState.colors[s][p][0],
                pcrState.colors[s][p][1],
                pcrState.colors[s][p][2],
                1.0f));
        }
        model->particleColorReplacements.push_back(pcs);
    }
    model->replaceParticleColors = true;
}

} // anonymous namespace

namespace ViewportOptionsPanel
{

void draw(DrawContext& ctx)
{
    // ---- Background ----
    if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Draw Grid", ctx.drawGrid);
        ImGui::Checkbox("Checkerboard Background", &SceneRenderer::state().drawCheckerBg);
        if (ImGui::Checkbox("Gradient Background", &SceneRenderer::state().drawGradientBg))
        {
            if (SceneRenderer::state().drawGradientBg)
                SceneRenderer::state().drawCheckerBg = false;
        }
        if (SceneRenderer::state().drawGradientBg)
        {
            ImGui::ColorEdit3("Gradient Top", &SceneRenderer::state().gradientTop.x);
            ImGui::ColorEdit3("Gradient Bottom", &SceneRenderer::state().gradientBottom.x);
        }
        ImGui::ColorEdit3("Background Color", &ctx.bgColor->x);

        ImGui::Spacing();
        ImGui::Text("Palette:");
        for (int i = 0; i < g_bgPaletteCount; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::ColorButton("##pal",
                    ImVec4(g_bgPalette[i].x, g_bgPalette[i].y, g_bgPalette[i].z, 1.0f),
                    ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20)))
                *ctx.bgColor = g_bgPalette[i];
            ImGui::PopID();
            if (i < g_bgPaletteCount - 1) ImGui::SameLine();
        }

        ImGui::Spacing();
        ImGui::Text("Camera Presets:");
        if (ImGui::Button("Front"))
            ctx.camera->setYawAndPitch(0.f, 90.f);
        ImGui::SameLine();
        if (ImGui::Button("Side"))
            ctx.camera->setYawAndPitch(270.f, 90.f);
        ImGui::SameLine();
        if (ImGui::Button("Back"))
            ctx.camera->setYawAndPitch(180.f, 90.f);
        ImGui::SameLine();
        if (ImGui::Button("Iso"))
            ctx.camera->setYawAndPitch(315.f, 90.f);
        if (ImGui::Button("Top"))
            ctx.camera->setYawAndPitch(ctx.camera->yaw(), 179.f);
        ImGui::SameLine();
        if (ImGui::Button("Bottom"))
            ctx.camera->setYawAndPitch(ctx.camera->yaw(), 1.f);
        ImGui::SameLine();
        if (ImGui::Button("Reset Camera"))
            ctx.resetCamera();
    }

    // ---- Lighting ----
    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Enable Lighting", &SceneRenderer::state().light.enabled);

        if (!SceneRenderer::state().light.enabled)
            ImGui::BeginDisabled();

        static const char* lightTypeNames[] = { "Directional", "Point", "Spot", "Ambient Only" };
        ImGui::Combo("##LightType",
                     reinterpret_cast<int*>(&SceneRenderer::state().light.type),
                     lightTypeNames, IM_ARRAYSIZE(lightTypeNames));

        if (SceneRenderer::state().light.type == SceneRenderer::LIGHT_DIRECTIONAL ||
            SceneRenderer::state().light.type == SceneRenderer::LIGHT_SPOT)
            ImGui::DragFloat3("Dir XYZ", SceneRenderer::state().light.direction, 0.01f, -5.0f, 5.0f, "%.2f");

        if (SceneRenderer::state().light.type == SceneRenderer::LIGHT_POINT ||
            SceneRenderer::state().light.type == SceneRenderer::LIGHT_SPOT)
            ImGui::DragFloat3("Pos XYZ", SceneRenderer::state().light.position, 0.1f, -50.0f, 50.0f, "%.1f");

        if (SceneRenderer::state().light.type == SceneRenderer::LIGHT_SPOT)
        {
            ImGui::SliderFloat("Cutoff Angle", &SceneRenderer::state().light.spotCutoff,
                               1.0f, 90.0f, "%.1f deg");
            ImGui::SliderFloat("Exponent", &SceneRenderer::state().light.spotExponent,
                               0.0f, 128.0f, "%.1f");
        }

        ImGui::ColorEdit3("Diffuse",  SceneRenderer::state().light.diffuse,  ImGuiColorEditFlags_Float);
        ImGui::ColorEdit3("Ambient",  SceneRenderer::state().light.ambient,  ImGuiColorEditFlags_Float);
        ImGui::ColorEdit3("Specular", SceneRenderer::state().light.specular, ImGuiColorEditFlags_Float);
        ImGui::SliderFloat("Intensity", &SceneRenderer::state().light.intensity, 0.0f, 3.0f, "%.2f");

        if (!SceneRenderer::state().light.enabled)
            ImGui::EndDisabled();

        ImGui::Spacing();
        if (ImGui::Button("Default##light", ImVec2(-1, 0)))
            SceneRenderer::state().light = SceneRenderer::LightSettings{};
        if (ImGui::Button("Bright Daylight", ImVec2(-1, 0)))
        {
            auto& l = SceneRenderer::state().light;
            l.direction[0] = -0.5f; l.direction[1] = 1.0f;
            l.direction[2] = -0.3f; l.direction[3] = 0.0f;
            l.diffuse[0] = 1.0f; l.diffuse[1] = 0.98f; l.diffuse[2] = 0.92f;
            l.ambient[0] = 0.45f; l.ambient[1] = 0.45f; l.ambient[2] = 0.50f;
            l.specular[0] = 0.3f; l.specular[1] = 0.3f; l.specular[2] = 0.3f;
            l.intensity = 1.2f;
            l.enabled = true;
        }
        if (ImGui::Button("Warm Sunset", ImVec2(-1, 0)))
        {
            auto& l = SceneRenderer::state().light;
            l.direction[0] = -1.0f; l.direction[1] = 0.3f;
            l.direction[2] = -0.5f; l.direction[3] = 0.0f;
            l.diffuse[0] = 1.0f; l.diffuse[1] = 0.65f; l.diffuse[2] = 0.3f;
            l.ambient[0] = 0.25f; l.ambient[1] = 0.2f; l.ambient[2] = 0.25f;
            l.specular[0] = 0.1f; l.specular[1] = 0.05f; l.specular[2] = 0.0f;
            l.intensity = 1.0f;
            l.enabled = true;
        }
        if (ImGui::Button("Cool Moonlight", ImVec2(-1, 0)))
        {
            auto& l = SceneRenderer::state().light;
            l.direction[0] = 0.3f; l.direction[1] = 1.0f;
            l.direction[2] = -0.7f; l.direction[3] = 0.0f;
            l.diffuse[0] = 0.6f; l.diffuse[1] = 0.65f; l.diffuse[2] = 0.8f;
            l.ambient[0] = 0.15f; l.ambient[1] = 0.15f; l.ambient[2] = 0.2f;
            l.specular[0] = 0.0f; l.specular[1] = 0.0f; l.specular[2] = 0.0f;
            l.intensity = 0.7f;
            l.enabled = true;
        }
        if (ImGui::Button("Flat (No Shading)", ImVec2(-1, 0)))
        {
            auto& l = SceneRenderer::state().light;
            l.direction[0] = 0.0f; l.direction[1] = 0.0f;
            l.direction[2] = -1.0f; l.direction[3] = 0.0f;
            l.diffuse[0] = 1.0f; l.diffuse[1] = 1.0f; l.diffuse[2] = 1.0f;
            l.ambient[0] = 1.0f; l.ambient[1] = 1.0f; l.ambient[2] = 1.0f;
            l.specular[0] = 0.0f; l.specular[1] = 0.0f; l.specular[2] = 0.0f;
            l.intensity = 0.5f;
            l.enabled = true;
        }
    }

    // ---- Model Control ----
    if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
    {
        WoWModel* mModel = ctx.getLoadedModel();
        if (mModel)
        {
            ImGui::Text("Name: %s", mModel->name().c_str());
            if (mModel->gamefile)
            {
                ImGui::Text("File: %s", mModel->gamefile->fullname().c_str());
                ImGui::Text("FileDataID: %d", mModel->gamefile->fileDataId());
            }
            ImGui::Text("V:%u  B:%u  T:%u  A:%zu  G:%zu",
                mModel->header.nVertices, mModel->header.nBones,
                mModel->header.nTextures, mModel->anims.size(), mModel->geosets.size());

            ImGui::Separator();
            ImGui::Checkbox("Render", &mModel->showModel);
            ImGui::SameLine();
            ImGui::Checkbox("Wireframe", &mModel->showWireframe);
            ImGui::Checkbox("Texture", &mModel->showTexture);
            ImGui::SameLine();
            ImGui::Checkbox("Bones", &mModel->showBones);
            ImGui::Checkbox("Bounds", &mModel->showBounds);
            ImGui::SameLine();
            ImGui::Checkbox("Particles", &mModel->showParticles);

            int alphaPercent = static_cast<int>(mModel->alpha_ * 100.0f);
            if (ImGui::SliderInt("Alpha", &alphaPercent, 0, 100))
                mModel->alpha_ = alphaPercent / 100.0f;
            ImGui::SliderFloat("Scale", &mModel->scale_, 0.1f, 3.0f, "%.2f");

            ImGui::DragFloat3("Position", &mModel->pos_.x, 0.1f);
            ImGui::DragFloat3("Rotation", &mModel->rot_.x, 1.0f);

            // Geosets
            if (ctx.geosetGroups && !ctx.geosetGroups->empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Geosets (click to toggle)");
                for (auto& group : *ctx.geosetGroups)
                {
                    if (ImGui::TreeNode(group.name.c_str()))
                    {
                        for (auto& ge : group.geosets)
                        {
                            bool displayed = mModel->isGeosetDisplayed(static_cast<uint>(ge.index));
                            ImVec4 color = displayed
                                ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                            ImGui::PushStyleColor(ImGuiCol_Text, color);
                            ImGuiTreeNodeFlags flags =
                                ImGuiTreeNodeFlags_Leaf |
                                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                ImGuiTreeNodeFlags_SpanAvailWidth;
                            ImGui::TreeNodeEx(ge.label.c_str(), flags);
                            if (ImGui::IsItemClicked())
                                mModel->showGeoset(static_cast<uint>(ge.index), !displayed);
                            ImGui::PopStyleColor();
                        }
                        ImGui::TreePop();
                    }
                }
            }

            // Particle Color Replacement
            if (ctx.pcrState)
            {
                auto& pcr = *ctx.pcrState;
                bool anyPCR = pcr.hasSet[0] || pcr.hasSet[1] || pcr.hasSet[2];
                if (anyPCR)
                {
                    ImGui::Separator();
                    if (ImGui::Checkbox("Replace Particle Colors", &pcr.enabled))
                    {
                        if (pcr.enabled)
                            applyParticleColors(mModel, pcr);
                        else
                        {
                            mModel->replaceParticleColors = false;
                            if (ctx.selectedSkin && *ctx.selectedSkin >= 0 && ctx.applySkin)
                                ctx.applySkin(mModel, *ctx.selectedSkin);
                        }
                    }

                    static const char* setNames[] = {"ID 11", "ID 12", "ID 13"};
                    static const char* phaseNames[] = {"Start", "Mid", "End"};
                    for (int s = 0; s < 3; ++s)
                    {
                        if (!pcr.hasSet[s]) continue;
                        ImGui::Text("%s", setNames[s]);
                        for (int p = 0; p < 3; ++p)
                        {
                            std::string label = std::format("{} {}##pcr{}{}",
                                setNames[s], phaseNames[p], s, p);
                            if (ImGui::ColorEdit3(label.c_str(), pcr.colors[s][p]))
                            {
                                if (pcr.enabled)
                                    applyParticleColors(mModel, pcr);
                            }
                        }
                    }
                }
            }
        }
        else
        {
            ImGui::TextDisabled("No model loaded.");
        }
    }
}

} // namespace ViewportOptionsPanel
