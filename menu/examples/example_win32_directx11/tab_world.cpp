#include "gui.h"
#include "gui_colors.h"

void ui::tabs::world(const TabCategory tab)
{
    if (tab.name == "world")
    {
        if (ui::begin_child_left("main", 280))
        {
            static bool enable_world_visuals = true;
            ImGui::Checkbox("enable world visuals", &enable_world_visuals);

            static bool show_items = true;
            static float items_color[4] = { 0.2f, 1.f, 0.2f, 1.f };

            static bool show_weapons = true;
            static float weapons_color[4] = { 1.f, 0.7f, 0.2f, 1.f };

            static bool show_grenades = true;
            static float grenades_color[4] = { 1.f, 0.2f, 0.2f, 1.f };

            ImGui::CheckPicker("show dropped items", "items color", &show_items, items_color);
            ImGui::CheckPicker("show dropped weapons", "weapons color", &show_weapons, weapons_color);
            ImGui::CheckPicker("show grenades", "grenades color", &show_grenades, grenades_color);
        }
        ImGui::EndChild();

        if (ui::begin_child_right("effects", 280))
        {
            static bool night_mode = false;
            static float night_color[4] = { 0.05f, 0.05f, 0.1f, 1.f };

            static bool fog = false;
            static float fog_density = 0.5f;

            ImGui::Checkbox("night mode", &night_mode);
            if (night_mode)
                ImGui::ColorEdit4("night color", night_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

            ImGui::Checkbox("fog", &fog);
            if (fog)
                ImGui::SliderFloat("fog density", &fog_density, 0.f, 1.f, "%.2f");
        }
        ImGui::EndChild();

        if (ui::begin_child_left("misc", 280))
        {
            static bool bullet_tracers = false;
            static float tracer_color[4] = { 1.f, 1.f, 0.f, 1.f };

            static bool hit_markers = true;
            static bool hit_sounds = true;

            ImGui::CheckPicker("bullet tracers", "tracer color", &bullet_tracers, tracer_color);
            ImGui::Checkbox("hit markers", &hit_markers);
            ImGui::Checkbox("hit sounds", &hit_sounds);
        }
        ImGui::EndChild();

        if (ui::begin_child_right("debug", 280))
        {
            static bool draw_radar = true;
            static bool draw_bomb_timer = true;

            ImGui::Checkbox("draw radar", &draw_radar);
            ImGui::Checkbox("draw bomb timer", &draw_bomb_timer);
        }
        ImGui::EndChild();
    }
}
