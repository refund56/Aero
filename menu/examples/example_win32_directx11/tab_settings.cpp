#include "gui.h"
#include "gui_colors.h"

void ui::tabs::settings(const TabCategory tab)
{
    if (tab.name == "settings")
    {
        if (ui::begin_child_left("General", 280))
        {
            static bool   CHECKBOX = false;
            static int    SLIDER_INT = 50;
            static float  SLIDER_FLOAT = 25.f;
            static int    COMBOBOX = 0;
            static const char* COMBO_ITEMS[] = { "none", "bravo", "alpha", "wraith", "smoke" };
            static bool   MULTICOMBO[5] = { false, false, false, false, false };

            ImGui::Checkbox("Checkbox", &CHECKBOX);
            ImGui::SliderInt("Slider int", &SLIDER_INT, 0, 100, "%d%%");
            ImGui::SliderFloat("Slider float", &SLIDER_FLOAT, 0.f, 100.f, "%.1f");
            ImGui::Combo("Combo box", &COMBOBOX, COMBO_ITEMS, IM_ARRAYSIZE(COMBO_ITEMS));
            ImGui::MultiCombo("Multi combo", MULTICOMBO, COMBO_ITEMS, IM_ARRAYSIZE(COMBO_ITEMS));
            ImGui::Button("Reset top", ImVec2(275, 30));
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Input", 280))
        {
            static int   KEYBIND = 0;
            static char  INPUT_TEXT[128] = "input text";
            static bool  CHECKBOX_PICKER = false;
            static float CHECKBOX_PICKER_COL[4] = { 1.f, 1.f, 1.f, 1.f };

            ImGui::Keybind("Keybind", &KEYBIND, true);
            ImGui::InputText("Input label", INPUT_TEXT, IM_ARRAYSIZE(INPUT_TEXT));
            ImGui::CheckPicker("Check picker", "Picker col", &CHECKBOX_PICKER, CHECKBOX_PICKER_COL);

            static float  SLIDER_FLOAT = 1.f;

            ImGui::SliderFloat("Menu transparency", &ui::UI_ALPHA, 0.5f, 1.f, "%.1f");

            if(!ImGui::SliderFloat("DPI Scale", &SLIDER_FLOAT, 1.f, 2.f, "%.1f"))
                dpi_scale = ImLerp(dpi_scale, SLIDER_FLOAT, ImGui::GetIO().DeltaTime * 8.f);

            ImGui::ColorEdit4("Menu color", (float*)&ui::colors::main,
                ImGuiColorEditFlags_NoSidePreview |
                ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_AlphaPreview);
            ImGui::Button("Reset right", ImVec2(275, 30));
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Advanced", 280))
        {
            static bool   BOTTOM_CHECKBOX = true;
            static int    BOTTOM_INT = 10;
            static float  BOTTOM_FLOAT = 0.5f;
            static int    BOTTOM_COMBO = 2;
            static const char* BOTTOM_ITEMS[] = { "one", "two", "three" };

            ImGui::Checkbox("Bottom checkbox", &BOTTOM_CHECKBOX);
            ImGui::SliderInt("Bottom slider", &BOTTOM_INT, 0, 20);
            ImGui::SliderFloat("Bottom float", &BOTTOM_FLOAT, 0.f, 1.f, "%.2f");
            ImGui::Combo("Bottom combo", &BOTTOM_COMBO, BOTTOM_ITEMS, IM_ARRAYSIZE(BOTTOM_ITEMS));
            ImGui::Button("Reset bottom", ImVec2(275, 30));
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Notifications", 280))
        {
            if (ImGui::Button("Notify 1", ImVec2(275, 30)))
                ui::add_notification("A", "Notification 1 triggered!", ImVec4(0.56f, 0.93f, 0.56f, 1.0f));
            if (ImGui::Button("Notify 2", ImVec2(275, 30)))
                ui::add_notification("B", "Notification 2 triggered!", ImVec4(0.98f, 0.95f, 0.57f, 1.0f));
            if (ImGui::Button("Notify 3", ImVec2(275, 30)))
                ui::add_notification("C", "Notification 3 triggered!", ImVec4(0.68f, 0.85f, 0.90f, 1.0f));
            if (ImGui::Button("Notify 4", ImVec2(275, 30)))
                ui::add_notification("D", "Notification 4 triggered!", ImVec4(0.99f, 0.60f, 0.60f, 1.0f));
            
        }
        ImGui::EndChild();
    }
}
