#define IMGUI_DEFINE_MATH_OPERATORS
#include <stdafx.hpp>

#include "gui_colors.h"
#include "gui_font.h"
#include "gui_image.h"
#include "gui_sounds.h"
#include "imgui_freetype.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <wincodec.h>
#include <algorithm>
#include <ranges>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "windowscodecs.lib")

namespace ui {
    // Define global pointers to be assigned in render.cpp
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
    float UI_ALPHA = 0.9f;
    float dpi_scale = 1.0f;
    std::vector<TabCategory> categories;
    std::vector<float> tab_lerp_values;
    int selected_tab_index = 0;
    bool has_scrollbar = false;
    int child_width = 320;

    struct Fonts {
        ImFont* spacegrotesk_medium[3];
        ImFont* montserrat_semibold[3];
        ImFont* tab_icon;
        ImFont* widget_icon[3];
        ImFont* notification_icon;
    } font;

    namespace images {
        ID3D11ShaderResourceView* background = nullptr;
    }

    struct Notification {
        std::string message;
        std::string icon;
        float alpha = 0.0f;
        float timer = 0.0f;
        bool fading_out = false;
        float duration = 3.0f;
        float target_y = 0.0f;
        float current_y = 0.0f;
        ImVec4 bg_color = ui::colors::background_dark;
        ImVec4 icon_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    };

    std::vector<Notification> notifications;

    static float left_cursor_y = 0.0f;
    static float right_cursor_y = 0.0f;
    static ImVec2 base_pos_left = ImVec2(0, 0);
    static ImVec2 base_pos_right = ImVec2(0, 0);
    static float spacing_x = 12.0f;
    static float spacing_y = 8.0f;

    bool instant_switch = true;

    // Helper function to load texture using Windows Imaging Component (WIC)
    bool CreateTextureFromMemory(ID3D11Device* device, const unsigned char* data, size_t size, ID3D11ShaderResourceView** out_srv)
    {
        if (!device) return false;

        // Ensure COM is initialized
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IWICImagingFactory* factory = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return false;

        IWICStream* stream = nullptr;
        hr = factory->CreateStream(&stream);
        if (FAILED(hr)) { factory->Release(); return false; }

        hr = stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size));
        if (FAILED(hr)) { stream->Release(); factory->Release(); return false; }

        IWICBitmapDecoder* decoder = nullptr;
        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) { stream->Release(); factory->Release(); return false; }

        IWICBitmapFrameDecode* frame = nullptr;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) { decoder->Release(); stream->Release(); factory->Release(); return false; }

        IWICFormatConverter* converter = nullptr;
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) { frame->Release(); decoder->Release(); stream->Release(); factory->Release(); return false; }

        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) { converter->Release(); frame->Release(); decoder->Release(); stream->Release(); factory->Release(); return false; }

        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        std::vector<BYTE> pixels(width * height * 4);
        hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) { converter->Release(); frame->Release(); decoder->Release(); stream->Release(); factory->Release(); return false; }

        D3D11_SUBRESOURCE_DATA init_data{};
        init_data.pSysMem = pixels.data();
        init_data.SysMemPitch = width * 4;

        ID3D11Texture2D* texture = nullptr;
        hr = device->CreateTexture2D(&desc, &init_data, &texture);
        if (FAILED(hr)) { converter->Release(); frame->Release(); decoder->Release(); stream->Release(); factory->Release(); return false; }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;

        hr = device->CreateShaderResourceView(texture, &srv_desc, out_srv);
        texture->Release();

        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();

        return SUCCEEDED(hr);
    }

    std::string to_lower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    void initialize_images()
    {
        if (images::background == nullptr && g_pd3dDevice != nullptr)
        {
            CreateTextureFromMemory(g_pd3dDevice, wallpaper, sizeof(wallpaper), &images::background);
        }
    }

    void initialize_fonts()
    {
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.IniFilename = NULL;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        ImFontConfig config;
        config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_LoadColor;
        config.Density = ui::dpi_scale;
        config.OversampleH = 1;
        config.OversampleV = 1;

        font.montserrat_semibold[0] = io.Fonts->AddFontFromMemoryTTF(PoppinsMedium, sizeof(PoppinsMedium), 22, &config, io.Fonts->GetGlyphRangesCyrillic());
        font.spacegrotesk_medium[0] = io.Fonts->AddFontFromMemoryTTF(PoppinsMedium, sizeof(PoppinsMedium), 24.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        font.spacegrotesk_medium[1] = io.Fonts->AddFontFromMemoryTTF(PoppinsMedium, sizeof(PoppinsMedium), 16.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        font.spacegrotesk_medium[2] = io.Fonts->AddFontFromMemoryTTF(PoppinsMedium, sizeof(PoppinsMedium), 20.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        font.tab_icon = io.Fonts->AddFontFromMemoryTTF(tab_font, sizeof(tab_font), 16.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        font.widget_icon[0] = io.Fonts->AddFontFromMemoryTTF(tab_font, sizeof(tab_font), 12.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        font.widget_icon[1] = io.Fonts->AddFontFromMemoryTTF(tab_font, sizeof(tab_font), 16.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
        font.notification_icon = io.Fonts->AddFontFromMemoryTTF(notification_font, sizeof(notification_font), 12.0f, &config, io.Fonts->GetGlyphRangesCyrillic());

        io.FontDefault = font.spacegrotesk_medium[1];
    }

    void items::text_shadow(ImDrawList* draw_list, ImVec2 pos, ImColor text_color, const char* text, ImFont* f, float font_size)
    {
        ImVec2 shadow_offset = ImVec2(1.0f, 1.0f);
        ImColor shadow_color = IM_COL32(0, 0, 0, text_color.Value.w * 255);

        if (f == nullptr)
            f = ImGui::GetFont();

        if (font_size == 0.0f)
            font_size = ImGui::GetFontSize();

        draw_list->AddText(f, font_size, ImVec2(pos.x + shadow_offset.x, pos.y + shadow_offset.y), shadow_color, text);
        draw_list->AddText(f, font_size, pos, text_color, text);
    }

    void add_notification(const std::string& icon, const std::string& msg, const ImVec4& icon_color) {
        notifications.push_back({ msg, icon, 0.0f, 0.0f, false, 3.0f, 0.0f, 0.0f,
                                   ImVec4(0.12f, 0.12f, 0.12f, 1.0f), icon_color });
    }

    void render_notification() {
        const float padding = 14.0f;
        const float right_padding = 10.0f;
        const ImVec2 viewport_pos = ImGui::GetMainViewport()->WorkPos;
        const ImVec2 viewport_size = ImGui::GetMainViewport()->Size;
        float total_offset = 0.0f;

        for (size_t i = 0; i < notifications.size(); ++i) {
            Notification& note = notifications[i];
            float delta = ImGui::GetIO().DeltaTime;
            note.timer += delta;

            if (!note.fading_out) {
                note.alpha = ImClamp(note.alpha + delta * 3.0f, 0.0f, 1.0f);
                if (note.timer >= note.duration)
                    note.fading_out = true;
            }
            else {
                note.alpha = ImClamp(note.alpha - delta * 2.0f, 0.0f, 1.0f);
            }

            if (note.fading_out && note.alpha <= 0.0f) {
                notifications.erase(notifications.begin() + i);
                --i;
                continue;
            }

            ImGui::PushFont(font.notification_icon);
            ImVec2 icon_size = ImGui::CalcTextSize(note.icon.c_str());
            ImGui::PopFont();

            ImGui::PushFont(font.spacegrotesk_medium[1]);
            ImVec2 text_size = ImGui::CalcTextSize(note.message.c_str());
            ImGui::PopFont();

            ImVec2 box_size(icon_size.x + text_size.x + 30.0f + right_padding, std::max(icon_size.y, text_size.y) + 10.0f);

            float target_y = viewport_pos.y + padding + total_offset;
            if (note.target_y == 0.0f)
                note.target_y = target_y;

            note.target_y = target_y;
            note.current_y += (note.target_y - note.current_y) * delta * 10.0f;

            ImVec2 pos(viewport_pos.x + viewport_size.x - box_size.x - padding, note.current_y);

            ImDrawList* draw = ImGui::GetForegroundDrawList();
            draw->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + box_size.x, pos.y + box_size.y), ImColor(note.bg_color.x, note.bg_color.y, note.bg_color.z, note.alpha), 6.0f);
            draw->AddRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + box_size.x, pos.y + box_size.y), ImGui::GetColorU32(ui::colors::outline), 6.0f);

            ImGui::PushFont(font.notification_icon);
            draw->AddText(
                ImVec2(pos.x + 10, pos.y + 7),
                ImColor(note.icon_color.x, note.icon_color.y, note.icon_color.z, note.alpha),
                note.icon.c_str()
            );
            ImGui::PopFont();

            float line_x = pos.x + 15 + icon_size.x + 5.0f;
            float line_y_start = pos.y + 5.0f;
            float line_y_end = pos.y + box_size.y - 5.0f;
            draw->AddLine(
                ImVec2(line_x - 1, line_y_start),
                ImVec2(line_x - 1, line_y_end),
                ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, note.alpha * 0.3f)),
                1.0f
            );

            ImGui::PushFont(font.spacegrotesk_medium[1]);
            draw->AddText(
                ImVec2(line_x + 8, pos.y + 5),
                ImColor(1.0f, 1.0f, 1.0f, note.alpha),
                note.message.c_str()
            );
            ImGui::PopFont();

            total_offset += box_size.y + 5.0f;
        }
    }

    float get_tab_alpha(int index)
    {
        float target = (index == selected_tab_index) ? 1.0f : 0.0f;

        if (instant_switch)
            return target;

        float speed = 6.0f;
        float delta = ImGui::GetIO().DeltaTime;

        float& value = tab_lerp_values[index];
        value = ImLerp(value, target, 1.0f - expf(-speed * delta));

        if (fabsf(value - target) < 0.001f)
            value = target;

        return value;
    }

    void reset_positions(ImVec2 base_left, ImVec2 base_right, float space_x, float space_y)
    {
        base_pos_left = base_left;
        base_pos_right = base_right;
        spacing_x = space_x;
        spacing_y = space_y;
        left_cursor_y = 0.0f;
        right_cursor_y = 0.0f;
    }

    bool begin_child_left(const char* id, int height)
    {
        ImGui::SetCursorPos(ImVec2(base_pos_left.x, base_pos_left.y + left_cursor_y));
        bool ret = ImGui::BeginChild(id, id, ImVec2(child_width, (float)height), true, ImGuiWindowFlags_NoScrollbar);
        
        left_cursor_y += (float)height + spacing_y;
        return ret;
    }

    bool begin_child_right(const char* id, int height)
    {
        ImGui::SetCursorPos(ImVec2(base_pos_right.x, base_pos_right.y + right_cursor_y));
        bool ret = ImGui::BeginChild(id, id, ImVec2(child_width, (float)height), true, ImGuiWindowFlags_NoScrollbar);
        
        right_cursor_y += (float)height + spacing_y;
        return ret;
    }

    void render_tabs_content()
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();

        ImVec2 content_pos(pos.x - 9, pos.y + 21);
        ImVec2 content_size(ui::size.x + 18, 502);

        static float current_width = 320.0f;
        float target_width = 312.0f;

        float lerp_speed = 10.0f;
        float delta = ImGui::GetIO().DeltaTime;

        current_width = ImLerp(current_width, target_width, ImClamp(delta * lerp_speed, 0.0f, 1.0f));
        child_width = (int)current_width;

        ImGui::SetCursorScreenPos(content_pos);

        if (ImGui::BeginChild("", "main_content", content_size, false, ImGuiWindowFlags_NoBackground))
        {
            has_scrollbar = ImGui::GetCurrentWindow()->ScrollbarY;

            float spacing_x_val = 12.0f;
            float spacing_y_val = 40.0f;

            ImVec2 base_left(20.0f, 11.0f);
            ImVec2 base_right(20.0f + child_width + spacing_x_val, 11.0f);
            reset_positions(base_left, base_right, spacing_x_val, spacing_y_val);

            for (size_t i = 0; i < categories.size(); ++i)
            {
                const auto& tab = categories[i];
                float alpha = get_tab_alpha((int)i);

                if (alpha <= 0.001f)
                    continue;

                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                ui::tabs::aimbot(tab);
                ui::tabs::visuals(tab);
                ui::tabs::world(tab);
                ui::tabs::settings(tab);
                ImGui::PopStyleVar();
            }
        }
        ImGui::EndChild();
    }

    void render_watermark()
    {
        const auto global_vars = g::memory.read<std::uintptr_t>(g::offsets.global_vars);
        if (!global_vars)
            return;

        // Get local player name
        std::string username = "player";
        if (systems::g_local.valid())
        {
            const auto controller = systems::g_local.controller();
            if (controller)
            {
                const auto name_ptr = g::memory.read<std::uintptr_t>(controller + SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash));
                if (name_ptr)
                {
                    username = g::memory.read_string(name_ptr, 128);
                }
            }
        }

        // Get triggerbot delay from settings
        static int last_valid_delay = 0;
        int trigger_delay = last_valid_delay;
        const auto& ctx = features::combat::g_shared.ctx();

        if (ctx.valid && cstypes::is_weapon_valid(ctx.weapon_type))
        {
            const auto& cfg = settings::g_combat.get(ctx.weapon_type);
            trigger_delay = cfg.triggerbot.delay;
            last_valid_delay = trigger_delay;
        }
        else if (systems::g_local.valid() && cstypes::is_weapon_valid(systems::g_local.weapon_type()))
        {
            const auto& cfg = settings::g_combat.get(systems::g_local.weapon_type());
            trigger_delay = cfg.triggerbot.delay;
            last_valid_delay = trigger_delay;
        }
        else
        {
            // If not in game, show the currently selected weapon group in the menu
            if (!systems::g_local.valid())
            {
                const auto& cfg = settings::g_combat.groups[g::menu.get_weapon_group()];
                trigger_delay = cfg.triggerbot.delay;
                last_valid_delay = trigger_delay;
            }
            else
            {
                // In game, but holding knife/grenade/etc.
                trigger_delay = last_valid_delay;
            }
        }

        // Get tickrate
        float interval = g::memory.read<float>(global_vars + 0x14);
        int tickrate = 64;
        if (interval > 0.0001f && interval < 1.0f)
        {
            tickrate = (int)std::round(1.0f / interval);
        }
        if (tickrate < 10 || tickrate > 256)
        {
            tickrate = 64;
        }

        // Get time
        std::time_t t = std::time(nullptr);
        std::tm tm;
        localtime_s(&tm, &t);
        char time_str[32];
        sprintf_s(time_str, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

        // Get overlay FPS
        int fps = (int)std::round(ImGui::GetIO().Framerate);

        // Build string: aero | [username] | delay: [trigger_delay]ms | [tickrate]tick | [fps]fps | [time]
        char watermark_buf[256];
        sprintf_s(watermark_buf, "aero | %s | delay: %dms | %dtick | %dfps | %s", username.c_str(), trigger_delay, tickrate, fps, time_str);

        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        ImGui::PushFont(font.spacegrotesk_medium[1]);
        ImVec2 text_size = ImGui::CalcTextSize(watermark_buf);
        ImGui::PopFont();

        float screen_w = ImGui::GetIO().DisplaySize.x;
        float padding_x = 8.0f;
        float padding_y = 4.0f;
        
        // Position at top right
        ImVec2 bg_min = ImVec2(screen_w - text_size.x - padding_x * 2.0f - 10.0f, 10.0f);
        ImVec2 bg_max = ImVec2(screen_w - 10.0f, 10.0f + text_size.y + padding_y * 2.0f);

        // Draw background box
        draw_list->AddRectFilled(bg_min, bg_max, ImGui::GetColorU32(ui::colors::background), ui::rounding);
        draw_list->AddRect(bg_min, bg_max, ImGui::GetColorU32(ui::colors::outline), ui::rounding);

        // Draw text
        ImGui::PushFont(font.spacegrotesk_medium[1]);
        ImVec2 text_pos = ImVec2(bg_min.x + padding_x, bg_min.y + padding_y + 1.0f);
        ui::items::text_shadow(draw_list, text_pos, ImColor(255, 255, 255, 240), watermark_buf);
        ImGui::PopFont();
    }

    void render_background()
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        ImVec2 center = ImVec2(size.x / 2.f, size.y / 2.f);
        float max_dist = sqrtf(center.x * center.x + center.y * center.y);

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            pos,
            ImVec2(pos.x + ui::size.x, pos.y + ui::size.y),
            ImColor(ui::colors::background),
            ui::rounding,
            ImDrawFlags_RoundCornersAll);

        const float spacing = 30.0f;

        ImVec4 base_color = ui::colors::main;

        float time = (float)ImGui::GetTime();

        for (float y = 0; y < size.y; y += spacing)
        {
            for (float x = 0; x < size.x; x += spacing)
            {
                float dist = sqrtf((x - center.x) * (x - center.x) + (y - center.y) * (y - center.y));
                float base_alpha = 0.1f * (1.0f - dist / max_dist);
                base_alpha = ImClamp(base_alpha, 0.0f, 1.0f);
                float alpha = base_alpha * (0.5f + 0.5f * sinf(time * 2.0f + (x + y) * 0.05f));

                if (alpha > 0.005f)
                {
                    ImU32 col_outer = ImGui::GetColorU32(ImVec4(base_color.x, base_color.y, base_color.z, alpha));
                    ImVec2 circle_pos = ImVec2(pos.x + x + 1.0f, pos.y + y + 1.0f);

                    ImGui::GetBackgroundDrawList()->AddCircle(circle_pos, 2.5f, col_outer, 0, 1.0f);
                }
            }
        }

        ImVec2 rect_top_pos = ImVec2(pos.x, pos.y);
        ImVec2 rect_top_end = ImVec2(rect_top_pos.x + ui::size.x, rect_top_pos.y + 32);
        ImGui::GetBackgroundDrawList()->AddRectFilled(rect_top_pos, rect_top_end, ImColor(ui::colors::background), ui::rounding, ImDrawFlags_RoundCornersTop);

        ImVec2 line_top_start(rect_top_pos.x + 1, rect_top_end.y - 1);
        ImVec2 line_top_end(rect_top_pos.x + ui::size.x - 1, rect_top_end.y - 1);
        ImGui::GetBackgroundDrawList()->AddLine(line_top_start, line_top_end, ImColor(ui::colors::outline), 1.0f);

        ImVec2 rect_pos = ImVec2(pos.x, pos.y + size.y - 39);
        ImVec2 rect_end = ImVec2(rect_pos.x + ui::size.x, rect_pos.y + 39);
        ImGui::GetBackgroundDrawList()->AddRectFilled(rect_pos, rect_end, ImColor(ui::colors::background), ui::rounding, ImDrawFlags_RoundCornersBottom);

        ImVec2 line_start(rect_pos.x + 1, rect_pos.y);
        ImVec2 line_end(rect_pos.x + ui::size.x - 2, rect_pos.y);
        ImGui::GetBackgroundDrawList()->AddLine(line_start, line_end, ImColor(ui::colors::outline), 1.0f);
    }

    void render_title()
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();

        ImGui::PushFont(font.montserrat_semibold[0]);

        const float padding_right = 13.0f;
        const float padding_bottom = 13.0f;

        const char* title = "Aero";
        float text_height = ImGui::GetFontSize();
        float text_width = ImGui::CalcTextSize(title).x;
        float text_x = pos.x + size.x - text_width - padding_right;
        float text_y = pos.y + size.y - text_height - padding_bottom;

        ui::items::text_shadow(ImGui::GetBackgroundDrawList(), ImVec2(text_x, text_y), ImColor(ui::colors::text), title);
        ImGui::PopFont();
    }

    void render_title_cheat(std::string game_name)
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();

        ImGui::PushFont(font.spacegrotesk_medium[1]);

        const float top_height = 32.0f;
        const float left_padding = 13.0f;

        float text_height = ImGui::GetFontSize();
        float centered_y = pos.y + (top_height - text_height) * 0.5f;
        float text_x = pos.x + left_padding;

        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        ImVec2 cursor_pos = ImVec2(text_x, centered_y - 1);

        ImVec4 col_start = ui::colors::text;
        ImVec4 col_end = ui::colors::main;

        int length = (int)game_name.size();

        for (int i = 0; i < length; i++)
        {
            float t = (length > 1) ? (float)i / (length - 1) : 0.0f;
            ImVec4 col = ImVec4(
                col_start.x + (col_end.x - col_start.x) * t,
                col_start.y + (col_end.y - col_start.y) * t,
                col_start.z + (col_end.z - col_start.z) * t,
                1.0f);

            char letter[2] = { game_name[i], '\0' };
            ui::items::text_shadow(draw_list, cursor_pos, ImColor(col), letter);
            ImVec2 letter_size = ImGui::CalcTextSize(letter);
            cursor_pos.x += letter_size.x;
        }

        ImGui::PopFont();
    }

    void render_build_date()
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();

        ImGui::PushFont(font.spacegrotesk_medium[1]);

        const float top_height = 32.0f;
        const float right_padding = 13.0f;

        float text_height = ImGui::GetFontSize();
        float centered_y = pos.y + (top_height - text_height) * 0.5f;

        std::string build_date = std::string("build: ") + __DATE__;
        std::string build_date_str = to_lower(build_date);
        float text_width = ImGui::CalcTextSize(build_date_str.c_str()).x;
        float text_x = pos.x + size.x - right_padding - text_width;
        ImVec2 text_pos = ImVec2(text_x, centered_y - 1);

        ui::items::text_shadow(ImGui::GetBackgroundDrawList(), text_pos, ImColor(ui::colors::text), build_date_str.c_str());

        // Draw Unload Button next to build date
        float button_w = 60.0f;
        float button_h = 18.0f;
        float button_x = text_x - button_w - 12.0f;
        float button_y = pos.y + (top_height - button_h) * 0.5f;

        ImGui::SetCursorScreenPos(ImVec2(button_x, button_y));
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.20f, 0.20f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

        if (ImGui::Button("unload", ImVec2(button_w, button_h)))
        {
            std::exit(0);
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImGui::PopFont();
    }

    void initialize_tabs()
    {
        categories.clear();
        categories.push_back(TabCategory{ "aimbot", "A" });
        categories.push_back(TabCategory{ "visuals", "B" });
        categories.push_back(TabCategory{ "world", "C" });
        categories.push_back(TabCategory{ "settings", "D" });

        tab_lerp_values.resize(categories.size(), 0.0f);
    }

    bool is_tab_selected(const std::string& tab_name) {
        if (selected_tab_index >= 0 && selected_tab_index < static_cast<int>(categories.size())) {
            return categories[selected_tab_index].name == tab_name;
        }
        return false;
    }

    void render_tabs(float dt)
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();

        float tab_height = 39.0f;
        ImVec2 rect_pos = ImVec2(pos.x, pos.y + size.y - tab_height);

        float padding_x = 15.0f;
        float spacing = 30.0f;
        float space_between_icon_and_text = 5.0f;

        float x = rect_pos.x + padding_x;
        float y_center = rect_pos.y + tab_height * 0.5f - 1;

        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        bool mouse_hand_set = false;

        ImVec4 base_color = ui::colors::text;
        ImVec4 highlight_color = ui::colors::main;

        float anim_speed = 12.0f;

        for (size_t i = 0; i < categories.size(); ++i)
        {
            float target = (selected_tab_index == (int)i) ? 1.0f : 0.0f;
            tab_lerp_values[i] = ImLerp(tab_lerp_values[i], target, dt * anim_speed);
        }

        for (size_t i = 0; i < categories.size(); ++i)
        {
            const auto& tab = categories[i];
            std::string icon = tab.icon;
            std::string name = tab.name;

            ImGui::PushFont(font.tab_icon);
            ImVec2 icon_size = ImGui::CalcTextSize(icon.c_str());
            ImGui::PopFont();

            ImGui::PushFont(font.spacegrotesk_medium[1]);
            ImVec2 name_size = ImGui::CalcTextSize(name.c_str());
            ImGui::PopFont();

            float icon_offset_y = 0.0f;
            if (icon == "A") icon_offset_y = 0.0f;
            else if (icon == "B") icon_offset_y = 2.0f;
            else if (icon == "C") icon_offset_y = 1.0f;
            else if (icon == "D") icon_offset_y = 2.0f;

            ImVec2 icon_pos = ImVec2(x, y_center - icon_size.y * 0.5f + icon_offset_y);
            ImVec2 name_pos = ImVec2(icon_pos.x + icon_size.x + space_between_icon_and_text, y_center - name_size.y * 0.5f);

            ImRect icon_rect(icon_pos, ImVec2(icon_pos.x + icon_size.x, icon_pos.y + icon_size.y));
            ImRect name_rect(name_pos, ImVec2(name_pos.x + name_size.x, name_pos.y + name_size.y));

            ImVec4 lerp_color = ImLerp(base_color, highlight_color, tab_lerp_values[i]);
            ImColor color(lerp_color.x, lerp_color.y, lerp_color.z, lerp_color.w);

            ui::items::text_shadow(ImGui::GetBackgroundDrawList(), icon_pos, color, icon.c_str(), font.tab_icon);
            ui::items::text_shadow(ImGui::GetBackgroundDrawList(), name_pos, color, name.c_str(), font.spacegrotesk_medium[1]);

            if (icon_rect.Contains(mouse_pos) || name_rect.Contains(mouse_pos))
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                mouse_hand_set = true;

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    selected_tab_index = (int)i;
            }

            x += icon_size.x + space_between_icon_and_text + name_size.x + spacing;
        }

        if (!mouse_hand_set)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    void render_outline()
    {
        ImVec2 pos(ImGui::GetWindowPos());
        ImVec2 size(pos.x + ui::size.x, pos.y + ui::size.y);

        ImGui::GetBackgroundDrawList()->AddRect(
            pos,
            size,
            IM_COL32(
                ui::colors::outline.x * 255,
                ui::colors::outline.y * 255,
                ui::colors::outline.z * 255,
                20
            ),
            ui::rounding - 1,
            ImDrawFlags_RoundCornersAll);
    }
}

namespace config
{
    inline std::filesystem::path get_config_dir()
    {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, path)))
        {
            return std::filesystem::path(path) / "Aero";
        }
        return "";
    }

    inline void unpack_sounds()
    {
        auto dir = get_config_dir() / "sounds";
        std::filesystem::create_directories(dir);

        struct SoundToUnpack {
            std::string name;
            const unsigned char* data;
            size_t size;
        };

        std::vector<SoundToUnpack> sounds = {
            { "bell.wav", resources::sounds::bell, resources::sounds::bell_size },
            { "bubble.wav", resources::sounds::bubble, resources::sounds::bubble_size },
            { "cod.wav", resources::sounds::cod, resources::sounds::cod_size },
            { "cricketbathittingsound.wav", resources::sounds::cricketbathittingsound, resources::sounds::cricketbathittingsound_size },
            { "fxxxxlity.wav", resources::sounds::fxxxxlity, resources::sounds::fxxxxlity_size },
            { "headshot1.wav", resources::sounds::headshot1, resources::sounds::headshot1_size },
            { "skxxt.wav", resources::sounds::skxxt, resources::sounds::skxxt_size },
            { "xxxxlose.wav", resources::sounds::xxxxlose, resources::sounds::xxxxlose_size }
        };

        for (const auto& sound : sounds)
        {
            auto path = dir / sound.name;
            if (!std::filesystem::exists(path))
            {
                std::ofstream file(path, std::ios::binary);
                if (file.is_open())
                {
                    file.write(reinterpret_cast<const char*>(sound.data), sound.size);
                    file.close();
                }
            }
        }
    }

    inline void create_config_dir()
    {
        auto dir = get_config_dir();
        if (!dir.empty())
        {
            std::filesystem::create_directories(dir);
            unpack_sounds();
        }
    }

    inline std::vector<std::string> get_configs()
    {
        std::vector<std::string> list;
        auto dir = get_config_dir();
        if (dir.empty()) return list;

        create_config_dir();

        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".cfg")
                {
                    list.push_back(entry.path().stem().string());
                }
            }
        } catch (...) {}

        return list;
    }

    inline bool save(const std::string& name)
    {
        auto dir = get_config_dir();
        if (dir.empty()) return false;

        create_config_dir();

        std::filesystem::path file_path = dir / (name + ".cfg");
        std::ofstream file(file_path, std::ios::binary);
        if (!file.is_open()) return false;

        file.write(reinterpret_cast<const char*>(&settings::g_combat), sizeof(settings::g_combat));
        file.write(reinterpret_cast<const char*>(&settings::g_esp), sizeof(settings::g_esp));
        file.write(reinterpret_cast<const char*>(&settings::g_misc), sizeof(settings::g_misc));
        file.write(reinterpret_cast<const char*>(&ui::UI_ALPHA), sizeof(ui::UI_ALPHA));
        file.write(reinterpret_cast<const char*>(&ui::colors::main), sizeof(ui::colors::main));
        return true;
    }

    inline bool load(const std::string& name)
    {
        auto dir = get_config_dir();
        if (dir.empty()) return false;

        std::filesystem::path file_path = dir / (name + ".cfg");
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&settings::g_combat), sizeof(settings::g_combat));
        file.read(reinterpret_cast<char*>(&settings::g_esp), sizeof(settings::g_esp));
        file.read(reinterpret_cast<char*>(&settings::g_misc), sizeof(settings::g_misc));
        file.read(reinterpret_cast<char*>(&ui::UI_ALPHA), sizeof(ui::UI_ALPHA));
        file.read(reinterpret_cast<char*>(&ui::colors::main), sizeof(ui::colors::main));
        return true;
    }
}

// -------------------------------------------------------------
// IMPLEMENTATION OF CUSTOM TABS BOUND TO ACTUAL SETTINGS
// -------------------------------------------------------------

void ui::tabs::aimbot(const TabCategory tab)
{
    if (tab.name == "aimbot")
    {
        auto& cfg = settings::g_combat.groups[g::menu.get_weapon_group()];

        if (ui::begin_child_left("Aimbot", 280))
        {
            ImGui::Checkbox("Enable Aimbot", &cfg.aimbot.enabled);
            ImGui::Keybind("Aimbot Key", &cfg.aimbot.key, true);

            ImGui::SliderInt("FOV", &cfg.aimbot.fov, 1, 45, "%d");
            ImGui::SliderInt("Smooth", &cfg.aimbot.smoothing, 0, 50, "%d");

            ImGui::Checkbox("Visible Only", &cfg.aimbot.visible_only);
            ImGui::Checkbox("Autowall", &cfg.aimbot.autowall);
            if (cfg.aimbot.autowall)
            {
                ImGui::SliderFloat("Min Damage", &cfg.aimbot.min_damage, 1.f, 100.f, "%.0f");
            }
            ImGui::Checkbox("Predictive", &cfg.aimbot.predictive);
            ImGui::Checkbox("Head Only", &cfg.aimbot.head_only);

            float fov_col[4] = { cfg.aimbot.fov_color.r / 255.f, cfg.aimbot.fov_color.g / 255.f, cfg.aimbot.fov_color.b / 255.f, cfg.aimbot.fov_color.a / 255.f };
            ImGui::CheckPicker("Draw FOV", "FOV Color", &cfg.aimbot.draw_fov, fov_col);
            cfg.aimbot.fov_color = zdraw::rgba{ static_cast<std::uint8_t>(fov_col[0] * 255.f), static_cast<std::uint8_t>(fov_col[1] * 255.f), static_cast<std::uint8_t>(fov_col[2] * 255.f), static_cast<std::uint8_t>(fov_col[3] * 255.f) };
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Triggerbot", 280))
        {
            ImGui::Checkbox("Enable Triggerbot", &cfg.triggerbot.enabled);
            ImGui::Keybind("Triggerbot Key", &cfg.triggerbot.key, true);

            ImGui::SliderFloat("Hitchance", &cfg.triggerbot.hitchance, 0.f, 100.f, "%.0f%%");
            ImGui::SliderInt("Delay (ms)", &cfg.triggerbot.delay, 0, 500, "%d");

            ImGui::Checkbox("Autowall##tb", &cfg.triggerbot.autowall);
            if (cfg.triggerbot.autowall)
            {
                ImGui::SliderFloat("Min Damage##tb", &cfg.triggerbot.min_damage, 1.f, 100.f, "%.0f");
            }

            ImGui::Checkbox("Autostop", &cfg.triggerbot.autostop);
            if (cfg.triggerbot.autostop)
            {
                ImGui::Checkbox("Early Autostop", &cfg.triggerbot.early_autostop);
            }
            ImGui::Checkbox("Predictive##tb", &cfg.triggerbot.predictive);
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Weapon Selection", 100))
        {
            const char* weapon_groups[] = { "Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "LMG" };
            int current_group = g::menu.get_weapon_group();
            if (ImGui::Combo("Weapon Group", &current_group, weapon_groups, IM_ARRAYSIZE(weapon_groups)))
            {
                g::menu.set_weapon_group(current_group);
            }
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Other Combat Settings", 150))
        {
            ImGui::Checkbox("Penetration Crosshair", &cfg.other.penetration_crosshair);

            float pen_yes[4] = { cfg.other.penetration_color_yes.r / 255.f, cfg.other.penetration_color_yes.g / 255.f, cfg.other.penetration_color_yes.b / 255.f, cfg.other.penetration_color_yes.a / 255.f };
            if (ImGui::ColorEdit4("Penetration Yes Color", pen_yes, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
            {
                cfg.other.penetration_color_yes = zdraw::rgba{ static_cast<std::uint8_t>(pen_yes[0] * 255.f), static_cast<std::uint8_t>(pen_yes[1] * 255.f), static_cast<std::uint8_t>(pen_yes[2] * 255.f), static_cast<std::uint8_t>(pen_yes[3] * 255.f) };
            }

            float pen_no[4] = { cfg.other.penetration_color_no.r / 255.f, cfg.other.penetration_color_no.g / 255.f, cfg.other.penetration_color_no.b / 255.f, cfg.other.penetration_color_no.a / 255.f };
            if (ImGui::ColorEdit4("Penetration No Color", pen_no, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
            {
                cfg.other.penetration_color_no = zdraw::rgba{ static_cast<std::uint8_t>(pen_no[0] * 255.f), static_cast<std::uint8_t>(pen_no[1] * 255.f), static_cast<std::uint8_t>(pen_no[2] * 255.f), static_cast<std::uint8_t>(pen_no[3] * 255.f) };
            }
        }
        ImGui::EndChild();
    }
}

void ui::tabs::visuals(const TabCategory tab)
{
    if (tab.name == "visuals")
    {
        auto& p = settings::g_esp.m_player;

        if (ui::begin_child_left("Player ESP", 280))
        {
            ImGui::Checkbox("Enable Player ESP", &p.enabled);

            float box_col[4] = { p.m_box.visible_color.r / 255.f, p.m_box.visible_color.g / 255.f, p.m_box.visible_color.b / 255.f, p.m_box.visible_color.a / 255.f };
            ImGui::CheckPicker("Bounding Box", "Box Color", &p.m_box.enabled, box_col);
            p.m_box.visible_color = zdraw::rgba{ static_cast<std::uint8_t>(box_col[0] * 255.f), static_cast<std::uint8_t>(box_col[1] * 255.f), static_cast<std::uint8_t>(box_col[2] * 255.f), static_cast<std::uint8_t>(box_col[3] * 255.f) };

            if (p.m_box.enabled)
            {
                const char* box_styles[] = { "Full", "Cornered" };
                int style = static_cast<int>(p.m_box.style);
                if (ImGui::Combo("Box Style", &style, box_styles, IM_ARRAYSIZE(box_styles)))
                {
                    p.m_box.style = static_cast<settings::esp::player::box::style_type>(style);
                }
                ImGui::Checkbox("Box Fill", &p.m_box.fill);
                ImGui::Checkbox("Box Outline", &p.m_box.outline);
                if (p.m_box.style == settings::esp::player::box::style_type::cornered)
                {
                    ImGui::SliderFloat("Corner Length", &p.m_box.corner_length, 4.f, 30.f, "%.0f");
                }
                
                float box_occ[4] = { p.m_box.occluded_color.r / 255.f, p.m_box.occluded_color.g / 255.f, p.m_box.occluded_color.b / 255.f, p.m_box.occluded_color.a / 255.f };
                if (ImGui::ColorEdit4("Box Occluded Color", box_occ, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                {
                    p.m_box.occluded_color = zdraw::rgba{ static_cast<std::uint8_t>(box_occ[0] * 255.f), static_cast<std::uint8_t>(box_occ[1] * 255.f), static_cast<std::uint8_t>(box_occ[2] * 255.f), static_cast<std::uint8_t>(box_occ[3] * 255.f) };
                }
            }

            float name_col[4] = { p.m_name.color.r / 255.f, p.m_name.color.g / 255.f, p.m_name.color.b / 255.f, p.m_name.color.a / 255.f };
            ImGui::CheckPicker("Player Name", "Name Color", &p.m_name.enabled, name_col);
            p.m_name.color = zdraw::rgba{ static_cast<std::uint8_t>(name_col[0] * 255.f), static_cast<std::uint8_t>(name_col[1] * 255.f), static_cast<std::uint8_t>(name_col[2] * 255.f), static_cast<std::uint8_t>(name_col[3] * 255.f) };
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Bars", 280))
        {
            float hp_col[4] = { p.m_health_bar.full_color.r / 255.f, p.m_health_bar.full_color.g / 255.f, p.m_health_bar.full_color.b / 255.f, p.m_health_bar.full_color.a / 255.f };
            ImGui::CheckPicker("Health Bar", "Health Color", &p.m_health_bar.enabled, hp_col);
            p.m_health_bar.full_color = zdraw::rgba{ static_cast<std::uint8_t>(hp_col[0] * 255.f), static_cast<std::uint8_t>(hp_col[1] * 255.f), static_cast<std::uint8_t>(hp_col[2] * 255.f), static_cast<std::uint8_t>(hp_col[3] * 255.f) };

            if (p.m_health_bar.enabled)
            {
                const char* positions[] = { "Left", "Top", "Bottom" };
                int pos = static_cast<int>(p.m_health_bar.position);
                if (ImGui::Combo("Health Position", &pos, positions, IM_ARRAYSIZE(positions)))
                {
                    p.m_health_bar.position = static_cast<settings::esp::player::health_bar::position_type>(pos);
                }
                ImGui::Checkbox("Health Outline", &p.m_health_bar.outline);
                ImGui::Checkbox("Health Gradient", &p.m_health_bar.gradient);
                ImGui::Checkbox("Health Value", &p.m_health_bar.show_value);
            }

            float ammo_col[4] = { p.m_ammo_bar.full_color.r / 255.f, p.m_ammo_bar.full_color.g / 255.f, p.m_ammo_bar.full_color.b / 255.f, p.m_ammo_bar.full_color.a / 255.f };
            ImGui::CheckPicker("Ammo Bar", "Ammo Color", &p.m_ammo_bar.enabled, ammo_col);
            p.m_ammo_bar.full_color = zdraw::rgba{ static_cast<std::uint8_t>(ammo_col[0] * 255.f), static_cast<std::uint8_t>(ammo_col[1] * 255.f), static_cast<std::uint8_t>(ammo_col[2] * 255.f), static_cast<std::uint8_t>(ammo_col[3] * 255.f) };

            if (p.m_ammo_bar.enabled)
            {
                const char* positions[] = { "Left", "Top", "Bottom" };
                int pos = static_cast<int>(p.m_ammo_bar.position);
                if (ImGui::Combo("Ammo Position", &pos, positions, IM_ARRAYSIZE(positions)))
                {
                    p.m_ammo_bar.position = static_cast<settings::esp::player::ammo_bar::position_type>(pos);
                }
                ImGui::Checkbox("Ammo Outline", &p.m_ammo_bar.outline);
                ImGui::Checkbox("Ammo Gradient", &p.m_ammo_bar.gradient);
                ImGui::Checkbox("Ammo Value", &p.m_ammo_bar.show_value);
            }
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Skeleton & Hitboxes", 280))
        {
            float skel_col[4] = { p.m_skeleton.visible_color.r / 255.f, p.m_skeleton.visible_color.g / 255.f, p.m_skeleton.visible_color.b / 255.f, p.m_skeleton.visible_color.a / 255.f };
            ImGui::CheckPicker("Skeleton", "Skeleton Color", &p.m_skeleton.enabled, skel_col);
            p.m_skeleton.visible_color = zdraw::rgba{ static_cast<std::uint8_t>(skel_col[0] * 255.f), static_cast<std::uint8_t>(skel_col[1] * 255.f), static_cast<std::uint8_t>(skel_col[2] * 255.f), static_cast<std::uint8_t>(skel_col[3] * 255.f) };

            if (p.m_skeleton.enabled)
            {
                ImGui::SliderFloat("Skeleton Thickness", &p.m_skeleton.thickness, 0.5f, 4.f, "%.1f");
                
                float skel_occ[4] = { p.m_skeleton.occluded_color.r / 255.f, p.m_skeleton.occluded_color.g / 255.f, p.m_skeleton.occluded_color.b / 255.f, p.m_skeleton.occluded_color.a / 255.f };
                if (ImGui::ColorEdit4("Skeleton Occluded Color", skel_occ, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                {
                    p.m_skeleton.occluded_color = zdraw::rgba{ static_cast<std::uint8_t>(skel_occ[0] * 255.f), static_cast<std::uint8_t>(skel_occ[1] * 255.f), static_cast<std::uint8_t>(skel_occ[2] * 255.f), static_cast<std::uint8_t>(skel_occ[3] * 255.f) };
                }
            }

            float hb_col[4] = { p.m_hitboxes.visible_color.r / 255.f, p.m_hitboxes.visible_color.g / 255.f, p.m_hitboxes.visible_color.b / 255.f, p.m_hitboxes.visible_color.a / 255.f };
            ImGui::CheckPicker("Hitboxes", "Hitbox Color", &p.m_hitboxes.enabled, hb_col);
            p.m_hitboxes.visible_color = zdraw::rgba{ static_cast<std::uint8_t>(hb_col[0] * 255.f), static_cast<std::uint8_t>(hb_col[1] * 255.f), static_cast<std::uint8_t>(hb_col[2] * 255.f), static_cast<std::uint8_t>(hb_col[3] * 255.f) };

            if (p.m_hitboxes.enabled)
            {
                ImGui::Checkbox("Hitbox Fill", &p.m_hitboxes.fill);
                ImGui::Checkbox("Hitbox Outline", &p.m_hitboxes.outline);
            }
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Weapon & Flags", 280))
        {
            float wp_col[4] = { p.m_weapon.text_color.r / 255.f, p.m_weapon.text_color.g / 255.f, p.m_weapon.text_color.b / 255.f, p.m_weapon.text_color.a / 255.f };
            ImGui::CheckPicker("Weapon ESP", "Weapon Color", &p.m_weapon.enabled, wp_col);
            p.m_weapon.text_color = zdraw::rgba{ static_cast<std::uint8_t>(wp_col[0] * 255.f), static_cast<std::uint8_t>(wp_col[1] * 255.f), static_cast<std::uint8_t>(wp_col[2] * 255.f), static_cast<std::uint8_t>(wp_col[3] * 255.f) };

            if (p.m_weapon.enabled)
            {
                const char* disp_types[] = { "Text", "Icon", "Text + Icon" };
                int type = static_cast<int>(p.m_weapon.display);
                if (ImGui::Combo("Weapon Display", &type, disp_types, IM_ARRAYSIZE(disp_types)))
                {
                    p.m_weapon.display = static_cast<settings::esp::player::weapon::display_type>(type);
                }
            }

            ImGui::Checkbox("Show Info Flags", &p.m_info_flags.enabled);
            if (p.m_info_flags.enabled)
            {
                const char* flag_names[] = { "Money", "Armor", "Kit", "Scoped", "Defusing", "Flashed", "Distance" };
                constexpr settings::esp::player::info_flags::flag flag_values[] = { settings::esp::player::info_flags::money, settings::esp::player::info_flags::armor, settings::esp::player::info_flags::kit, settings::esp::player::info_flags::scoped, settings::esp::player::info_flags::defusing, settings::esp::player::info_flags::flashed, settings::esp::player::info_flags::distance };
                
                static bool multiselect[7] = { false, false, false, false, false, false, false };
                for (int i = 0; i < 7; ++i)
                {
                    multiselect[i] = p.m_info_flags.has(flag_values[i]);
                }

                ImGui::MultiCombo("Active Flags", multiselect, flag_names, 7);

                std::uint8_t flags = 0;
                for (int i = 0; i < 7; ++i)
                {
                    if (multiselect[i])
                        flags |= flag_values[i];
                }
                p.m_info_flags.flags = flags;
            }
        }
        ImGui::EndChild();
    }
}

void ui::tabs::world(const TabCategory tab)
{
    if (tab.name == "world")
    {
        auto& it = settings::g_esp.m_item;
        auto& pr = settings::g_esp.m_projectile;

        if (ui::begin_child_left("Dropped Items", 280))
        {
            ImGui::Checkbox("Enable Item ESP", &it.enabled);
            ImGui::SliderFloat("Max Distance", &it.max_distance, 5.f, 150.f, "%.0fm");

            float icon_col[4] = { it.m_icon.color.r / 255.f, it.m_icon.color.g / 255.f, it.m_icon.color.b / 255.f, it.m_icon.color.a / 255.f };
            ImGui::CheckPicker("Show Icons", "Icon Color", &it.m_icon.enabled, icon_col);
            it.m_icon.color = zdraw::rgba{ static_cast<std::uint8_t>(icon_col[0] * 255.f), static_cast<std::uint8_t>(icon_col[1] * 255.f), static_cast<std::uint8_t>(icon_col[2] * 255.f), static_cast<std::uint8_t>(icon_col[3] * 255.f) };

            float name_col[4] = { it.m_name.color.r / 255.f, it.m_name.color.g / 255.f, it.m_name.color.b / 255.f, it.m_name.color.a / 255.f };
            ImGui::CheckPicker("Show Names", "Name Color", &it.m_name.enabled, name_col);
            it.m_name.color = zdraw::rgba{ static_cast<std::uint8_t>(name_col[0] * 255.f), static_cast<std::uint8_t>(name_col[1] * 255.f), static_cast<std::uint8_t>(name_col[2] * 255.f), static_cast<std::uint8_t>(name_col[3] * 255.f) };

            float ammo_col[4] = { it.m_ammo.color.r / 255.f, it.m_ammo.color.g / 255.f, it.m_ammo.color.b / 255.f, it.m_ammo.color.a / 255.f };
            ImGui::CheckPicker("Show Ammo", "Ammo Color", &it.m_ammo.enabled, ammo_col);
            it.m_ammo.color = zdraw::rgba{ static_cast<std::uint8_t>(ammo_col[0] * 255.f), static_cast<std::uint8_t>(ammo_col[1] * 255.f), static_cast<std::uint8_t>(ammo_col[2] * 255.f), static_cast<std::uint8_t>(ammo_col[3] * 255.f) };
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Item Filters", 280))
        {
            const char* filter_names[] = { "Rifles", "SMGs", "Shotguns", "Snipers", "Pistols", "Heavy", "Grenades", "Utility" };
            static bool filters[8] = { false, false, false, false, false, false, false, false };
            filters[0] = it.m_filters.rifles;
            filters[1] = it.m_filters.smgs;
            filters[2] = it.m_filters.shotguns;
            filters[3] = it.m_filters.snipers;
            filters[4] = it.m_filters.pistols;
            filters[5] = it.m_filters.heavy;
            filters[6] = it.m_filters.grenades;
            filters[7] = it.m_filters.utility;

            ImGui::MultiCombo("Filters", filters, filter_names, 8);

            it.m_filters.rifles = filters[0];
            it.m_filters.smgs = filters[1];
            it.m_filters.shotguns = filters[2];
            it.m_filters.snipers = filters[3];
            it.m_filters.pistols = filters[4];
            it.m_filters.heavy = filters[5];
            it.m_filters.grenades = filters[6];
            it.m_filters.utility = filters[7];
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Projectiles", 280))
        {
            ImGui::Checkbox("Enable Projectile ESP", &pr.enabled);
            ImGui::Checkbox("Show Projectile Icon", &pr.show_icon);
            ImGui::Checkbox("Show Projectile Name", &pr.show_name);
            ImGui::Checkbox("Show Timer Bar", &pr.show_timer_bar);
            ImGui::Checkbox("Show Inferno Bounds", &pr.show_inferno_bounds);
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Projectile Colors", 280))
        {
            float def_col[4] = { pr.default_color.r / 255.f, pr.default_color.g / 255.f, pr.default_color.b / 255.f, pr.default_color.a / 255.f };
            if (ImGui::ColorEdit4("Default Color", def_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                pr.default_color = zdraw::rgba{ static_cast<std::uint8_t>(def_col[0] * 255.f), static_cast<std::uint8_t>(def_col[1] * 255.f), static_cast<std::uint8_t>(def_col[2] * 255.f), static_cast<std::uint8_t>(def_col[3] * 255.f) };

            float he_col[4] = { pr.color_he.r / 255.f, pr.color_he.g / 255.f, pr.color_he.b / 255.f, pr.color_he.a / 255.f };
            if (ImGui::ColorEdit4("HE Grenade", he_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                pr.color_he = zdraw::rgba{ static_cast<std::uint8_t>(he_col[0] * 255.f), static_cast<std::uint8_t>(he_col[1] * 255.f), static_cast<std::uint8_t>(he_col[2] * 255.f), static_cast<std::uint8_t>(he_col[3] * 255.f) };

            float flash_col[4] = { pr.color_flash.r / 255.f, pr.color_flash.g / 255.f, pr.color_flash.b / 255.f, pr.color_flash.a / 255.f };
            if (ImGui::ColorEdit4("Flashbang", flash_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                pr.color_flash = zdraw::rgba{ static_cast<std::uint8_t>(flash_col[0] * 255.f), static_cast<std::uint8_t>(flash_col[1] * 255.f), static_cast<std::uint8_t>(flash_col[2] * 255.f), static_cast<std::uint8_t>(flash_col[3] * 255.f) };

            float smoke_col[4] = { pr.color_smoke.r / 255.f, pr.color_smoke.g / 255.f, pr.color_smoke.b / 255.f, pr.color_smoke.a / 255.f };
            if (ImGui::ColorEdit4("Smoke", smoke_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                pr.color_smoke = zdraw::rgba{ static_cast<std::uint8_t>(smoke_col[0] * 255.f), static_cast<std::uint8_t>(smoke_col[1] * 255.f), static_cast<std::uint8_t>(smoke_col[2] * 255.f), static_cast<std::uint8_t>(smoke_col[3] * 255.f) };

            float mol_col[4] = { pr.color_molotov.r / 255.f, pr.color_molotov.g / 255.f, pr.color_molotov.b / 255.f, pr.color_molotov.a / 255.f };
            if (ImGui::ColorEdit4("Molotov", mol_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                pr.color_molotov = zdraw::rgba{ static_cast<std::uint8_t>(mol_col[0] * 255.f), static_cast<std::uint8_t>(mol_col[1] * 255.f), static_cast<std::uint8_t>(mol_col[2] * 255.f), static_cast<std::uint8_t>(mol_col[3] * 255.f) };
        }
        ImGui::EndChild();
    }
}

void ui::tabs::settings(const TabCategory tab)
{
    if (tab.name == "settings")
    {
        auto& misc = settings::g_misc;

        if (ui::begin_child_left("Misc Settings", 280))
        {
            float gren_col[4] = { misc.m_grenades.color.r / 255.f, misc.m_grenades.color.g / 255.f, misc.m_grenades.color.b / 255.f, misc.m_grenades.color.a / 255.f };
            ImGui::CheckPicker("Grenade Prediction", "Prediction Color", &misc.m_grenades.enabled, gren_col);
            misc.m_grenades.color = zdraw::rgba{ static_cast<std::uint8_t>(gren_col[0] * 255.f), static_cast<std::uint8_t>(gren_col[1] * 255.f), static_cast<std::uint8_t>(gren_col[2] * 255.f), static_cast<std::uint8_t>(gren_col[3] * 255.f) };
            if (misc.m_grenades.enabled)
            {
                ImGui::Checkbox("Local Player Only", &misc.m_grenades.local_only);
            }

            ImGui::Checkbox("Nade Helper", &misc.m_nade_helper.enabled);

            ImGui::Checkbox("Limit FPS", &misc.limit_fps);
            if (misc.limit_fps)
            {
                ImGui::SliderInt("FPS Limit", &misc.fps_limit, 30, 1000);
            }
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Hitsounds", 95))
        {
            ImGui::Checkbox("Enable Hitsounds", &misc.m_hitsounds.enabled);
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Radar Settings", 200))
        {
            ImGui::Checkbox("Enable Radar", &misc.m_radar.enabled);
            if (misc.m_radar.enabled)
            {
                ImGui::SliderFloat("Radar Size", &misc.m_radar.size, 100.0f, 400.0f, "%.0f");
                ImGui::SliderFloat("Radar X Pos", &misc.m_radar.pos_x, 0.0f, 2000.0f, "%.0f");
                ImGui::SliderFloat("Radar Y Pos", &misc.m_radar.pos_y, 0.0f, 2000.0f, "%.0f");
                ImGui::SliderFloat("Radar Opacity", &misc.m_radar.opacity, 0.1f, 1.0f, "%.1f");
                ImGui::Checkbox("Show Enemies", &misc.m_radar.show_enemies);
                ImGui::Checkbox("Show Teammates", &misc.m_radar.show_teammates);
            }
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Visual Features", 230))
        {
            ImGui::Checkbox("Watermark", &misc.m_watermark.enabled);
            ImGui::Checkbox("Bomb Timer", &misc.m_bomb_timer.enabled);
            ImGui::Checkbox("Hitmarker", &misc.m_hitmarker.enabled);
            ImGui::Checkbox("Damage Indicator", &misc.m_damage_indicator.enabled);
            ImGui::Checkbox("Enable Spectator List", &misc.m_spectator_list.enabled);
            if (misc.m_spectator_list.enabled)
            {
                ImGui::SliderFloat("Spec List X", &misc.m_spectator_list.pos_x, 0.0f, 2000.0f, "%.0f");
                ImGui::SliderFloat("Spec List Y", &misc.m_spectator_list.pos_y, 0.0f, 2000.0f, "%.0f");
            }
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Menu Interface", 280))
        {
            ImGui::SliderFloat("Menu Transparency", &ui::UI_ALPHA, 0.5f, 1.0f, "%.1f");

            static float scale = 1.0f;
            if (!ImGui::SliderFloat("DPI Scale", &scale, 1.0f, 2.0f, "%.1f"))
            {
                dpi_scale = ImLerp(dpi_scale, scale, ImGui::GetIO().DeltaTime * 8.f);
            }

            ImGui::ColorEdit4("Menu Accent Color", (float*)&ui::colors::main,
                ImGuiColorEditFlags_NoSidePreview |
                ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_AlphaPreview);
        }
        ImGui::EndChild();

        if (ui::begin_child_right("Configurations", 280))
        {
            static char config_name[64] = "default";
            ImGui::InputText("Config Name", config_name, IM_ARRAYSIZE(config_name));

            // Dynamic list of configs updated regularly or via refresh
            static std::vector<std::string> configs = config::get_configs();
            static bool initialized = false;
            if (!initialized)
            {
                configs = config::get_configs();
                initialized = true;
            }

            static int selected_config_idx = 0;

            if (selected_config_idx >= (int)configs.size())
                selected_config_idx = 0;

            // Display a combo box with available configs
            std::vector<const char*> config_names_char;
            for (const auto& c : configs)
            {
                config_names_char.push_back(c.c_str());
            }

            if (!configs.empty())
            {
                if (ImGui::Combo("Select Config", &selected_config_idx, config_names_char.data(), (int)config_names_char.size()))
                {
                    if (selected_config_idx >= 0 && selected_config_idx < (int)configs.size())
                    {
                        strcpy_s(config_name, configs[selected_config_idx].c_str());
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("No configurations found.");
            }

            // Save and Load buttons
            if (ImGui::Button("Save Config", ImVec2(130, 30)))
            {
                if (strlen(config_name) > 0)
                {
                    if (config::save(config_name))
                    {
                        ui::add_notification("A", "Config saved successfully!", ImVec4(0.56f, 0.93f, 0.56f, 1.0f));
                        configs = config::get_configs();
                        for (int i = 0; i < (int)configs.size(); ++i)
                        {
                            if (configs[i] == config_name)
                            {
                                selected_config_idx = i;
                                break;
                            }
                        }
                    }
                    else
                    {
                        ui::add_notification("D", "Failed to save config.", ImVec4(0.99f, 0.60f, 0.60f, 1.0f));
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Load Config", ImVec2(130, 30)))
            {
                if (strlen(config_name) > 0)
                {
                    if (config::load(config_name))
                    {
                        ui::add_notification("A", "Config loaded successfully!", ImVec4(0.56f, 0.93f, 0.56f, 1.0f));
                    }
                    else
                    {
                        ui::add_notification("D", "Failed to load config.", ImVec4(0.99f, 0.60f, 0.60f, 1.0f));
                    }
                }
            }

            if (ImGui::Button("Refresh List", ImVec2(130, 30)))
            {
                configs = config::get_configs();
            }

            ImGui::SameLine();

            if (ImGui::Button("Open Folder", ImVec2(130, 30)))
            {
                config::create_config_dir();
                ShellExecuteW(NULL, L"open", config::get_config_dir().c_str(), NULL, NULL, SW_SHOWDEFAULT);
            }
        }
        ImGui::EndChild();

        if (ui::begin_child_left("Notifications", 280))
        {
            if (ImGui::Button("Test Success Notification", ImVec2(275, 30)))
                ui::add_notification("A", "Cheat settings loaded successfully!", ImVec4(0.56f, 0.93f, 0.56f, 1.0f));
            if (ImGui::Button("Test Warning Notification", ImVec2(275, 30)))
                ui::add_notification("B", "Low FPS detected, optimize settings.", ImVec4(0.98f, 0.95f, 0.57f, 1.0f));
            if (ImGui::Button("Test Info Notification", ImVec2(275, 30)))
                ui::add_notification("C", "Attached to process: cs2.exe", ImVec4(0.68f, 0.85f, 0.90f, 1.0f));
            if (ImGui::Button("Test Error Notification", ImVec2(275, 30)))
                ui::add_notification("D", "Failed to resolve RIP offset for csgo_input", ImVec4(0.99f, 0.60f, 0.60f, 1.0f));
        }
        ImGui::EndChild();
    }
}

// -------------------------------------------------------------
// MAIN DRAWING METHOD (replaces the original menu::draw)
// -------------------------------------------------------------

void menu::draw( )
{
    if ( GetAsyncKeyState( VK_INSERT ) & 1 )
    {
        this->m_open = !this->m_open;
    }

    // Initialize ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    {
        ui::UpdateMenuColors();

        if ( this->m_open )
        {
            ImGui::SetNextWindowSize(ui::size);
            ImGui::Begin("aero", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
            {
                ui::render_background();
                ui::render_title();
                ui::render_title_cheat("counter-strike 2");
                ui::render_build_date();
                ui::render_tabs_content();
                ui::render_tabs(ImGui::GetIO().DeltaTime);
                ui::render_outline();
                if (settings::g_misc.m_watermark.enabled)
                    ui::render_watermark();
                ui::render_notification();
            }
            ImGui::End();
        }
        else
        {
            if (settings::g_misc.m_watermark.enabled)
                ui::render_watermark();
            ui::render_notification();
        }
    }
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}