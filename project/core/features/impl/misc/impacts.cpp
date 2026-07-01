#include <stdafx.hpp>
#include <mmsystem.h>
#include <core/menu/gui_sounds.h>
#include <core/features/features.hpp>
#include <fstream>

#pragma comment(lib, "winmm.lib")

namespace features::misc {

    static std::wstring s_wav_path;

    static void ensure_wav_extracted()
    {
        if (!s_wav_path.empty())
            return;

        wchar_t temp[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, temp))
            return;

        s_wav_path = std::wstring(temp) + L"catalyst_hit.wav";

        std::ofstream file(s_wav_path, std::ios::binary | std::ios::trunc);
        if (file.is_open())
        {
            file.write(reinterpret_cast<const char*>(resources::sounds::skxxt), sizeof(resources::sounds::skxxt));
            file.close();
        }
        else
        {
            s_wav_path.clear();
        }
    }

    static void play_hitsound()
    {
        ensure_wav_extracted();
        if (s_wav_path.empty())
            return;

        PlaySoundW(s_wav_path.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }

    void impacts::tick()
    {
        bool enabled = settings::g_misc.m_hitsounds.enabled ||
                       settings::g_misc.m_hitmarker.enabled ||
                       settings::g_misc.m_damage_indicator.enabled;
        if (!enabled)
            return;

        if (!systems::g_local.valid())
            return;

        const auto local_team = systems::g_local.team();
        const auto local_pawn = systems::g_local.pawn();
        const auto players = systems::g_collector.players();

        struct hp { std::uintptr_t pawn; int health; math::vector3 origin; };
        static std::vector<hp> s_prev;

        std::vector<hp> now;
        now.reserve(players.size());

        for (const auto& p : players)
        {
            if (p.pawn == local_pawn) continue;
            if (p.team != local_team)
                now.push_back({ p.pawn, p.health, p.origin });
        }

        if (!s_prev.empty())
        {
            for (const auto& old : s_prev)
            {
                for (const auto& cur : now)
                {
                    if (cur.pawn == old.pawn && cur.health < old.health && old.health > 0)
                    {
                        int dmg = old.health - cur.health;
                        if (dmg > 0 && dmg <= 100)
                        {
                            // Trigger hitmarker
                            if (settings::g_misc.m_hitmarker.enabled)
                            {
                                g_visual_extras.register_hit();
                            }

                            // Trigger hitsound
                            if (settings::g_misc.m_hitsounds.enabled)
                            {
                                play_hitsound();
                            }

                            // Trigger damage indicator
                            if (settings::g_misc.m_damage_indicator.enabled)
                            {
                                math::vector3 hit_pos = cur.origin + math::vector3{ 0.0f, 0.0f, 50.0f };
                                g_visual_extras.add_damage_indicator(dmg, hit_pos);
                            }
                        }
                    }
                }
            }
        }

        s_prev = std::move(now);
    }

} // namespace features::misc