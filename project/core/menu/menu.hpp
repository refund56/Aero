#pragma once

#include "imgui.h"
#include <string>
#include <vector>

struct TabCategory
{
    std::string name;
    std::string icon;

    TabCategory(const std::string& n, const std::string& i)
        : name(n), icon(i)
    {
    }
};

class menu
{
public:
	void draw( );
	[[nodiscard]] bool is_open( ) const noexcept { return this->m_open; }
	
	[[nodiscard]] int get_weapon_group( ) const noexcept { return this->m_weapon_group; }
	void set_weapon_group( int group ) noexcept { this->m_weapon_group = group; }

private:
	bool m_open{ false };
	int m_weapon_group{ 0 };
};

namespace ui
{
    inline ImVec2 size = ImVec2(674, 560);
    inline float rounding = 6.f;
    extern float UI_ALPHA;
    extern float dpi_scale;

    void initialize_fonts();
    void initialize_images();
    void initialize_tabs();
    void render_background();
    void render_title();
    void render_title_cheat(std::string game_name);
    void render_build_date();
    void render_tabs(float dt);
    void render_tabs_content();
    void render_outline();
    void render_watermark();
    void render_notification();
    void add_notification(const std::string& icon, const std::string& msg, const ImVec4& icon_color);
    void UpdateMenuColors();

    namespace tabs
    {
        void aimbot(const TabCategory tab);
        void visuals(const TabCategory tab);
        void world(const TabCategory tab);
        void settings(const TabCategory tab);
    }

    namespace items
    {
        void text_shadow(ImDrawList* draw_list, ImVec2 pos, ImColor text_color, const char* text, ImFont* f = nullptr, float font_size = 0.0f);
    }
}