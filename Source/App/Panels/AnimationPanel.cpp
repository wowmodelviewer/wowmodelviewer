// ============================================================================
// AnimationPanel — animation control, playback, and skin/texture selection.
// ============================================================================
#include "AnimationPanel.h"

#include "imgui.h"
#include "WoWModel.h"

namespace AnimationPanel
{

void draw(DrawContext& ctx)
{
    WoWModel* aModel = ctx.getLoadedModel ? ctx.getLoadedModel() : nullptr;
    if (!aModel || !ctx.animEntries || ctx.animEntries->empty())
    {
        ImGui::TextDisabled("No model loaded.");
        return;
    }

    auto& animEntries         = *ctx.animEntries;
    int&  selectedAnimCombo   = *ctx.selectedAnimCombo;
    float& animSpeed          = *ctx.animSpeed;
    int&  loopCount           = *ctx.loopCount;
    bool& lockAnims           = *ctx.lockAnims;
    int&  selectedSecondaryAnim = *ctx.selectedSecondaryAnim;
    int&  selectedMouthAnim   = *ctx.selectedMouthAnim;
    float& mouthSpeed         = *ctx.mouthSpeed;

    // ---- Animation selector ----
    ImGui::SeparatorText("Animation");
    const char* previewAnim = (selectedAnimCombo >= 0 && selectedAnimCombo < static_cast<int>(animEntries.size()))
        ? animEntries[selectedAnimCombo].label.c_str() : "<none>";
    if (ImGui::BeginCombo("##AnimCombo", previewAnim))
    {
        for (int i = 0; i < static_cast<int>(animEntries.size()); ++i)
        {
            bool selected = (i == selectedAnimCombo);
            if (ImGui::Selectable(animEntries[i].label.c_str(), selected))
            {
                selectedAnimCombo = i;
                int idx = animEntries[i].animIndex;
                aModel->currentAnim = idx;
                aModel->animManager->SetAnim(0, idx, 0);
                aModel->animManager->Play();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // ---- Playback controls ----
    if (ImGui::Button("Play"))
        aModel->animManager->Play();
    ImGui::SameLine();
    if (ImGui::Button("Pause"))
        aModel->animManager->Pause();
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        aModel->animManager->Stop();
    ImGui::SameLine();
    if (ImGui::Button("<<"))
        aModel->animManager->PrevFrame();
    ImGui::SameLine();
    if (ImGui::Button(">>"))
        aModel->animManager->NextFrame();

    if (aModel->animManager->IsPaused())
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Paused");

    // ---- Speed ----
    if (ImGui::SliderFloat("Speed", &animSpeed, 0.0f, 4.0f, "%.2f"))
        aModel->animManager->SetSpeed(animSpeed);

    // ---- Frame scrubber ----
    int frameCount = static_cast<int>(aModel->animManager->GetFrameCount());
    int curFrame = static_cast<int>(aModel->animManager->GetFrame());
    if (frameCount > 0)
    {
        if (ImGui::SliderInt("Frame", &curFrame, 0, frameCount))
            aModel->animManager->SetFrame(static_cast<size_t>(curFrame));
    }
    else
    {
        ImGui::Text("Frame: %d", curFrame);
    }

    // ---- Loop count ----
    if (ImGui::SliderInt("Loops", &loopCount, 0, 9))
    {
        aModel->animManager->Stop();
        int idx = animEntries[selectedAnimCombo].animIndex;
        aModel->animManager->SetAnim(0, idx, static_cast<short>(loopCount));
        // Chain next animations if available
        int nextAnim = idx;
        for (int i = 1; i < 4; ++i)
        {
            if (nextAnim >= 0 && nextAnim < static_cast<int>(aModel->anims.size()))
            {
                nextAnim = aModel->anims[nextAnim].NextAnimation;
                if (nextAnim >= 0)
                    aModel->animManager->AddAnim(static_cast<unsigned int>(nextAnim), static_cast<short>(loopCount));
                else
                    break;
            }
            else
                break;
        }
        aModel->animManager->Play();
    }
    ImGui::SetItemTooltip("0 = infinite loop");

    // ---- Secondary / Mouth animations ----
    ImGui::SeparatorText("Body Animation");
    if (ImGui::Checkbox("Lock animations", &lockAnims))
    {
        if (lockAnims)
        {
            aModel->animManager->ClearSecondary();
            aModel->animManager->ClearMouth();
            selectedSecondaryAnim = -1;
            selectedMouthAnim = -1;
        }
    }
    ImGui::SetItemTooltip("Uncheck to enable independent upper body and mouth animations");

    if (!lockAnims)
    {
        // Secondary animation (upper body)
        {
            const char* previewSec = (selectedSecondaryAnim >= 0 && selectedSecondaryAnim < static_cast<int>(animEntries.size()))
                ? animEntries[selectedSecondaryAnim].label.c_str() : "<none>";
            if (ImGui::BeginCombo("Upper Body##SecAnim", previewSec))
            {
                if (ImGui::Selectable("<none>", selectedSecondaryAnim < 0))
                {
                    selectedSecondaryAnim = -1;
                    aModel->animManager->ClearSecondary();
                }
                for (int i = 0; i < static_cast<int>(animEntries.size()); ++i)
                {
                    bool selected = (i == selectedSecondaryAnim);
                    if (ImGui::Selectable(animEntries[i].label.c_str(), selected))
                    {
                        selectedSecondaryAnim = i;
                        aModel->animManager->SetSecondary(animEntries[i].animIndex);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // Mouth animation
        {
            const char* previewMouth = (selectedMouthAnim >= 0 && selectedMouthAnim < static_cast<int>(animEntries.size()))
                ? animEntries[selectedMouthAnim].label.c_str() : "<none>";
            if (ImGui::BeginCombo("Mouth##MouthAnim", previewMouth))
            {
                if (ImGui::Selectable("<none>", selectedMouthAnim < 0))
                {
                    selectedMouthAnim = -1;
                    aModel->animManager->ClearMouth();
                }
                for (int i = 0; i < static_cast<int>(animEntries.size()); ++i)
                {
                    bool selected = (i == selectedMouthAnim);
                    if (ImGui::Selectable(animEntries[i].label.c_str(), selected))
                    {
                        selectedMouthAnim = i;
                        aModel->animManager->SetMouth(animEntries[i].animIndex);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // Mouth speed
        if (ImGui::SliderFloat("Mouth Speed", &mouthSpeed, 0.0f, 4.0f, "%.2f"))
            aModel->animManager->SetMouthSpeed(mouthSpeed);
    }

    // ---- Skin selector ----
    if (!ctx.skinEntries || ctx.skinEntries->empty())
        return;

    auto& skinEntries = *ctx.skinEntries;
    int&  selectedSkin = *ctx.selectedSkin;

    ImGui::SeparatorText("Skin / Texture");
    const char* previewSkin = (selectedSkin >= 0 && selectedSkin < static_cast<int>(skinEntries.size()))
        ? skinEntries[selectedSkin].label.c_str() : "<none>";
    if (ImGui::BeginCombo("##SkinCombo", previewSkin))
    {
        for (int i = 0; i < static_cast<int>(skinEntries.size()); ++i)
        {
            bool selected = (i == selectedSkin);
            if (ImGui::Selectable(skinEntries[i].label.c_str(), selected))
            {
                if (ctx.applySkin)
                    ctx.applySkin(aModel, i);
                ctx.blpSkin[0] = ctx.blpSkin[1] = ctx.blpSkin[2] = -1;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Per-slot BLP skin selector
    size_t maxSlots = 0;
    for (const auto& se : skinEntries)
        if (se.count > maxSlots) maxSlots = se.count;
    if (maxSlots > 3) maxSlots = 3;

    if (maxSlots > 1)
    {
        const char* slotLabels[3] = { "Texture 1", "Texture 2", "Texture 3" };
        for (size_t slot = 0; slot < maxSlots; ++slot)
        {
            const char* preview = (ctx.blpSkin[slot] >= 0 && ctx.blpSkin[slot] < static_cast<int>(skinEntries.size()))
                ? skinEntries[ctx.blpSkin[slot]].label.c_str() : "(grouped)";
            char comboId[32];
            snprintf(comboId, sizeof(comboId), "##BLPSlot%zu", slot);
            if (ImGui::BeginCombo(slotLabels[slot], preview))
            {
                for (int i = 0; i < static_cast<int>(skinEntries.size()); ++i)
                {
                    if (!skinEntries[i].tex[0]) continue;
                    bool selected = (i == ctx.blpSkin[slot]);
                    if (ImGui::Selectable(skinEntries[i].label.c_str(), selected))
                    {
                        ctx.blpSkin[slot] = i;
                        aModel->updateTextureList(skinEntries[i].tex[0],
                            skinEntries[i].base + static_cast<int>(slot));
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
    }
}

} // namespace AnimationPanel
