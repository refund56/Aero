#include "gui.h"
#include "gui_colors.h"

void ui::tabs::aimbot(const TabCategory tab)
{
    if (tab.name == "aimbot")
    {
        if (ui::begin_child_left("main", 280))
        {
            static bool  enableAimbot = true;
            static int   aimbotKey = 0;
            static int   quickSwitchKey = 1;
            static int   aimbotMode = 0;
            static float aimFOV = 5.f;
            static float aimSmooth = 2.f;
            static bool  visibleOnly = true;
            static bool  ignoreFlash = false;
            static bool  ignoreSmoke = false;
            static bool  enableAntiRecoil = false;
            static float recoilControl = 0.5f;
            static int   backtrackTicks = 0;
            static float prediction = 0.0f;

            ImGui::Checkbox("Enable Aimbot", &enableAimbot);
            ImGui::Keybind("Aimbot Key", &aimbotKey, true);
            ImGui::Keybind("Quick Switch Key", &quickSwitchKey, true);

            const char* modes[] = { "Silent", "Legit", "Rage", "Trigger", "Burst" };
            ImGui::Combo("Aimbot Mode", &aimbotMode, modes, IM_ARRAYSIZE(modes));

            ImGui::SliderFloat("FOV", &aimFOV, 0.f, 30.f, "%.1f");
            ImGui::SliderFloat("Smooth", &aimSmooth, 1.f, 10.f, "%.1f");
            ImGui::SliderFloat("Prediction", &prediction, 0.f, 5.f, "%.2f");

            ImGui::Checkbox("Visible Only", &visibleOnly);
            ImGui::Checkbox("Ignore Flash", &ignoreFlash);
            ImGui::Checkbox("Ignore Smoke", &ignoreSmoke);

            ImGui::Checkbox("Enable Anti Recoil", &enableAntiRecoil);
            ImGui::SliderFloat("Recoil Control", &recoilControl, 0.f, 1.f, "%.2f");
            ImGui::SliderInt("Backtrack Ticks", &backtrackTicks, 0, 12);

            if (ImGui::Button("Reset Aimbot Settings", ImVec2(275, 30))) {}
        }
        ImGui::EndChild();

        if (ui::begin_child_right("targets", 280))
        {
            static bool hitboxes[5] = { true, false, false, false, false };
            static bool prioritizeClosest = true;
            static bool targetTeammates = false;
            static int  focusDistance = 50;
            static int  maxTargets = 3;
            const char* hitboxLabels[] = { "Head", "Chest", "Stomach", "Arms", "Legs" };
            const char* boneLabels[] = { "Eye", "Neck", "Pelvis", "Foot" };
            static bool boneSelection[4] = { false, false, false, false };

            ImGui::MultiCombo("Hitboxes", hitboxes, hitboxLabels, IM_ARRAYSIZE(hitboxLabels));
            ImGui::MultiCombo("Bones", boneSelection, boneLabels, IM_ARRAYSIZE(boneLabels));

            ImGui::Checkbox("Prioritize Closest", &prioritizeClosest);
            ImGui::Checkbox("Target Teammates", &targetTeammates);

            ImGui::SliderInt("Focus Distance", &focusDistance, 0, 200);
            ImGui::SliderInt("Max Targets", &maxTargets, 1, 10);
        }
        ImGui::EndChild();

        if (ui::begin_child_left("behavior", 280))
        {
            static bool autoFire = false;
            static bool autoScope = false;
            static bool autoStop = false;
            static int  fireMode = 0;
            static int  burstCount = 2;
            static float stopDistance = 0.0f;

            const char* fireModes[] = { "Single", "Burst", "Auto" };

            ImGui::Checkbox("Auto Fire", &autoFire);
            ImGui::Checkbox("Auto Scope", &autoScope);
            ImGui::Checkbox("Auto Stop", &autoStop);

            ImGui::Combo("Firing Mode", &fireMode, fireModes, IM_ARRAYSIZE(fireModes));
            ImGui::SliderInt("Burst Count", &burstCount, 1, 5);
            ImGui::SliderFloat("Stop Distance", &stopDistance, 0.f, 5.f, "%.1f");
        }
        ImGui::EndChild();

        if (ui::begin_child_right("debug", 280))
        {
            static bool  drawFOV = true;
            static float fovColor[4] = { 1.f, 1.f, 0.f, 1.f };
            static bool  showTargetDebug = false;
            static bool  showFPS = false;
            static int   logLevel = 0;
            static float debugScale = 1.f;
            const char* logLevels[] = { "Info", "Warning", "Error" };

            ImGui::CheckPicker("Draw FOV", "FOV Color", &drawFOV, fovColor);
            ImGui::Checkbox("Show Target Debug", &showTargetDebug);
            ImGui::Checkbox("Show FPS", &showFPS);

            ImGui::Combo("Log Level", &logLevel, logLevels, IM_ARRAYSIZE(logLevels));
            ImGui::SliderFloat("Debug Scale", &debugScale, 0.5f, 2.f, "%.2f");

            if (ImGui::Button("Clear Debug Logs", ImVec2(275, 30))) {}
        }
        ImGui::EndChild();
    }
}
