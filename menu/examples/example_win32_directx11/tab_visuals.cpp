#include "gui.h"
#include "gui_colors.h"

void ui::tabs::visuals(const TabCategory tab)
{
    if (tab.name == "visuals")
    {
        if (ui::begin_child_left("main", 280))
        {
            static bool enable_esp = true;
            ImGui::Checkbox("enable esp", &enable_esp);

            static bool box_esp = true;
            static float box_color[4] = { 1.f, 1.f, 1.f, 1.f };

            static bool name_esp = true;
            static float name_color[4] = { 1.f, 1.f, 1.f, 1.f };

            static bool health_bar = true;
            static float health_color[4] = { 0.f, 1.f, 0.f, 1.f };

            static bool armor_bar = false;
            static float armor_color[4] = { 0.f, 0.6f, 1.f, 1.f };

            static bool weapon_esp = false;
            static float weapon_color[4] = { 1.f, 0.8f, 0.4f, 1.f };

            static bool ammo_esp = false;
            static float ammo_color[4] = { 0.8f, 0.6f, 1.f, 1.f };

            static bool snaplines = false;
            static float snapline_color[4] = { 1.f, 1.f, 0.f, 1.f };

            ImGui::CheckPicker("bounding box", "box color", &box_esp, box_color);
            ImGui::CheckPicker("player name", "name color", &name_esp, name_color);
            ImGui::CheckPicker("health bar", "health color", &health_bar, health_color);
            ImGui::CheckPicker("armor bar", "armor color", &armor_bar, armor_color);
            ImGui::CheckPicker("weapon name", "weapon color", &weapon_esp, weapon_color);
            ImGui::CheckPicker("ammo count", "ammo color", &ammo_esp, ammo_color);
            ImGui::CheckPicker("snaplines", "line color", &snaplines, snapline_color);
        }
        ImGui::EndChild();

        if (ui::begin_child_right("effects", 280))
        {
            static bool glow_esp = false;
            static float glow_color[4] = { 1.f, 0.f, 0.f, 1.f };

            static bool chams = false;
            static float chams_color[4] = { 0.f, 1.f, 0.f, 1.f };

            static bool visible_only = true;
            static int max_distance = 150;

            ImGui::CheckPicker("glow", "glow color", &glow_esp, glow_color);
            ImGui::CheckPicker("chams", "chams color", &chams, chams_color);
            ImGui::Checkbox("visible only", &visible_only);
            ImGui::SliderInt("max distance", &max_distance, 10, 300, "%d m");
        }
        ImGui::EndChild();

        if (ui::begin_child_left("misc", 280))
        {
            static bool skeleton = false;
            static float skeleton_color[4] = { 1.f, 0.6f, 0.f, 1.f };

            static bool head_dot = false;
            static float head_dot_color[4] = { 1.f, 0.f, 0.f, 1.f };

            static bool out_of_fov_arrow = true;
            static float arrow_color[4] = { 1.f, 1.f, 0.f, 1.f };

            ImGui::CheckPicker("skeleton", "skeleton color", &skeleton, skeleton_color);
            ImGui::CheckPicker("head dot", "head dot color", &head_dot, head_dot_color);
            ImGui::CheckPicker("out of fov arrows", "arrow color", &out_of_fov_arrow, arrow_color);
        }
        ImGui::EndChild();

        if (ui::begin_child_right("debug", 280))
        {
            static bool draw_hitboxes = false;
            static bool show_dormant = false;
            static bool show_flags = true;

            ImGui::Checkbox("draw hitboxes", &draw_hitboxes);
            ImGui::Checkbox("show dormant players", &show_dormant);
            ImGui::Checkbox("show status flags", &show_flags);
        }
        ImGui::EndChild();
    }
}
