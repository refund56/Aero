#include <stdafx.hpp>
#include <shlwapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

namespace features::misc {

	void grenades::on_render( zdraw::draw_list& draw_list )
	{
		const auto& cfg = settings::g_misc.m_grenades;
		if ( !cfg.enabled )
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now( );
		const auto has_bvh = systems::g_bvh.valid( );

		if ( has_bvh )
		{
			this->update_in_flight( );
		}

		std::erase_if( this->m_in_flight, [ & ]( const in_flight_grenade& g )
			{
				if ( !g.detonated )
				{
					return false;
				}

				const auto fade_elapsed = std::chrono::duration<float>( now - g.detonate_time ).count( );
				if ( fade_elapsed <= 0.5f )
				{
					return false;
				}

				const auto projectiles = systems::g_collector.projectiles( );
				for ( const auto& proj : projectiles )
				{
					if ( proj.entity == g.entity )
					{
						return false;
					}
				}

				return true;
			} );

		for ( auto& gren : this->m_in_flight )
		{
			if ( !gren.traj.valid )
			{
				continue;
			}

			if ( !gren.detonated )
			{
				const auto elapsed = std::chrono::duration<float>( now - gren.throw_time ).count( );
				const auto fuse_expired = elapsed >= gren.traj.duration;
				const auto effect_started = gren.effect_started;

				if ( fuse_expired || effect_started )
				{
					gren.detonated = true;
					gren.detonate_time = now;
				}
			}

			auto alpha{ 1.0f };

			if ( gren.detonated )
			{
				const auto elapsed = std::chrono::duration<float>( now - gren.detonate_time ).count( );
				alpha = std::clamp( 1.0f - elapsed / 0.5f, 0.0f, 1.0f );
			}

			if ( alpha > 0.0f )
			{
				this->render_trajectory( draw_list, gren.traj, alpha );
			}
		}

		const auto& ctx = combat::g_shared.ctx( );
		auto holding_now{ false };

		if ( ctx.valid && ctx.weapon && ctx.weapon_type == cstypes::weapon_type::grenade )
		{
			const auto pin_pulled = g::memory.read<bool>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_hash ) );
			holding_now = pin_pulled;
		}

		if ( this->m_was_holding && !holding_now )
		{
			this->m_last_throw_time = now;
		}

		this->m_was_holding = holding_now;

		if ( !has_bvh || !this->can_predict( ) )
		{
			return;
		}

		this->update_weapon_properties( );

		math::vector3 origin{}, velocity{};
		this->setup_throw( origin, velocity );

		trajectory traj{};
		this->simulate( origin, velocity, traj );

		if ( traj.valid )
		{
			this->render_trajectory( draw_list, traj, 1.0f );
		}
	}

	bool grenades::can_predict( ) const
	{
		const auto& ctx = combat::g_shared.ctx( );
		if ( !ctx.valid || !ctx.weapon || !ctx.weapon_vdata )
		{
			return false;
		}

		if ( ctx.weapon_type != cstypes::weapon_type::grenade )
		{
			return false;
		}

		const auto pin_pulled = g::memory.read<bool>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_hash ) );
		if ( !pin_pulled )
		{
			const auto since_throw = std::chrono::duration<float>( std::chrono::steady_clock::now( ) - this->m_last_throw_time ).count( );
			if ( since_throw < throw_cooldown )
			{
				return false;
			}
		}

		const auto throw_time = g::memory.read<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_hash ) );
		if ( throw_time > 0.0f )
		{
			return false;
		}

		return true;
	}

	void grenades::update_weapon_properties( )
	{
		const auto& ctx = combat::g_shared.ctx( );
		if ( !ctx.weapon_vdata || ctx.weapon_vdata == this->m_weapon_vdata )
		{
			return;
		}

		this->m_weapon_vdata = ctx.weapon_vdata;
		this->m_throw_velocity = std::clamp( g::memory.read<float>( ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flThrowVelocity"_hash ) ), 1.0f, 10000.0f );

		const auto name_ptr = g::memory.read<std::uintptr_t>( ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_hash ) );
		if ( !name_ptr )
		{
			this->m_weapon_hash = 0;
			this->m_detonate_time = 1.5f;
			this->m_velocity_threshold = 0.1f;
			return;
		}

		char name[ 64 ]{};
		g::memory.read( name_ptr, name, sizeof( name ) - 1 );
		this->m_weapon_hash = fnv1a::runtime_hash( name );

		switch ( this->m_weapon_hash )
		{
		case "weapon_molotov"_hash:
		case "weapon_incgrenade"_hash:
			this->m_detonate_time = 2.0f;
			this->m_velocity_threshold = 0.0f;
			break;

		case "weapon_decoy"_hash:
			this->m_detonate_time = 2.0f;
			this->m_velocity_threshold = 0.2f;
			break;

		default:
			this->m_detonate_time = 1.5f;
			this->m_velocity_threshold = 0.1f;
			break;
		}
	}

	void grenades::setup_throw( math::vector3& origin, math::vector3& velocity )
	{
		const auto& ctx = combat::g_shared.ctx( );
		const auto pawn = systems::g_local.pawn( );

		auto strength{ 1.0f };

		const auto pin_pulled = g::memory.read<bool>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_hash ) );
		if ( pin_pulled )
		{
			strength = std::clamp( g::memory.read<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_hash ) ), 0.0f, 1.0f );
			if ( std::fabsf( strength - 0.5f ) <= 0.1f )
			{
				strength = 0.5f;
			}
		}

		auto angles = systems::g_view.angles( );

		if ( angles.x > 90.0f )
		{
			angles.x -= 360.0f;
		}
		else if ( angles.x < -90.0f )
		{
			angles.x += 360.0f;
		}

		angles.x -= ( 90.0f - std::fabsf( angles.x ) ) * 10.0f / 90.0f;

		const auto player_velocity = g::memory.read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );

		auto eye_pos = systems::g_view.origin( );
		eye_pos.z += strength * 12.0f - 12.0f;

		math::vector3 forward{};
		{
			const auto pitch = angles.x * ( std::numbers::pi_v<float> / 180.0f );
			const auto yaw = angles.y * ( std::numbers::pi_v<float> / 180.0f );

			forward =
			{
				std::cosf( pitch ) * std::cosf( yaw ),
				std::cosf( pitch ) * std::sinf( yaw ),
				-std::sinf( pitch )
			};
		}

		const auto hull_mins = math::vector3{ -k_hull_size, -k_hull_size, -k_hull_size };
		const auto hull_maxs = math::vector3{ k_hull_size, k_hull_size, k_hull_size };
		const auto trace = systems::g_bvh.trace_hull( eye_pos, eye_pos + forward * k_forward_offset, hull_mins, hull_maxs );
		origin = trace.end_pos - forward * k_pull_back;

		const auto throw_vel = std::clamp( this->m_throw_velocity * 0.9f, 15.0f, 750.0f );
		const auto throw_speed = ( strength * 0.7f + 0.3f ) * throw_vel;

		velocity = forward * throw_speed + player_velocity * k_velocity_inherit;
	}

	void grenades::update_in_flight( )
	{
		const auto& cfg = settings::g_misc.m_grenades;
		const auto projectiles = systems::g_collector.projectiles( );
		const auto controller = systems::g_local.controller( );

		if ( !controller )
		{
			return;
		}

		const auto local_pawn_handle = g::memory.read<std::uint32_t>( controller + SCHEMA( "CCSPlayerController", "m_hPlayerPawn"_hash ) );
		const auto now = std::chrono::steady_clock::now( );

		std::unordered_set<std::uintptr_t> alive{};

		for ( const auto& proj : projectiles )
		{
			if ( cfg.local_only && proj.thrower_handle != local_pawn_handle )
			{
				continue;
			}

			alive.insert( proj.entity );

			in_flight_grenade* existing{ nullptr };

			for ( auto& gren : this->m_in_flight )
			{
				if ( gren.entity == proj.entity )
				{
					existing = &gren;
					break;
				}
			}

			if ( existing )
			{
				existing->last_seen = now;

				if ( !existing->corrected )
				{
					const auto initial_pos = g::memory.read<math::vector3>( proj.entity + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialPosition"_hash ) );
					const auto initial_vel = g::memory.read<math::vector3>( proj.entity + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_hash ) );

					if ( initial_vel.length_sqr( ) >= 1.0f )
					{
						const auto saved = this->m_weapon_hash;
						this->m_weapon_hash = existing->weapon_hash;
						this->simulate( initial_pos, initial_vel, existing->traj );
						this->m_weapon_hash = saved;
						existing->corrected = true;
					}
				}

				if ( !existing->detonated && proj.effect_tick_begin > 0 )
				{
					existing->effect_started = true;
					existing->detonated = true;
					existing->detonate_time = now;
				}

				continue;
			}

			const auto initial_pos = g::memory.read<math::vector3>( proj.entity + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialPosition"_hash ) );
			const auto initial_vel = g::memory.read<math::vector3>( proj.entity + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_hash ) );

			if ( initial_vel.length_sqr( ) < 1.0f )
			{
				continue;
			}

			if ( proj.effect_tick_begin > 0 )
			{
				continue;
			}

			const auto wh = this->hash_from_projectile( proj.subtype );
			const auto saved = this->m_weapon_hash;
			this->m_weapon_hash = wh;

			in_flight_grenade gren{};
			gren.entity = proj.entity;
			gren.weapon_hash = wh;
			gren.throw_time = now;
			gren.last_seen = now;
			gren.corrected = true;
			this->simulate( initial_pos, initial_vel, gren.traj );

			this->m_weapon_hash = saved;
			this->m_in_flight.push_back( std::move( gren ) );
		}

		for ( auto& gren : this->m_in_flight )
		{
			if ( gren.detonated )
			{
				continue;
			}

			if ( alive.contains( gren.entity ) )
			{
				continue;
			}

			const auto missing_for = std::chrono::duration<float>( now - gren.last_seen ).count( );
			if ( missing_for >= missing_grace )
			{
				gren.detonated = true;
				gren.detonate_time = now;
			}
		}
	}

	std::uintptr_t grenades::hash_from_projectile( systems::collector::projectile_subtype type ) const
	{
		switch ( type )
		{
		case systems::collector::projectile_subtype::he_grenade:    return "weapon_hegrenade"_hash;
		case systems::collector::projectile_subtype::flashbang:     return "weapon_flashbang"_hash;
		case systems::collector::projectile_subtype::smoke_grenade: return "weapon_smokegrenade"_hash;
		case systems::collector::projectile_subtype::molotov:       return "weapon_molotov"_hash;
		case systems::collector::projectile_subtype::decoy:         return "weapon_decoy"_hash;
		default:                                                    return 0;
		}
	}

	void grenades::simulate( const math::vector3& start, const math::vector3& velocity, trajectory& out )
	{
		this->m_sv_gravity = systems::g_convars.get<float>( CONVAR( "sv_gravity"_hash ) );

		const auto molotov_slope = systems::g_convars.get<float>( CONVAR( "weapon_molotov_maxdetonateslope"_hash ) );
		this->m_molotov_max_slope_z = std::cosf( molotov_slope * std::numbers::pi_v<float> / 180.0f );

		out.points.clear( );
		out.points.reserve( max_ticks / ticks_per_point );
		out.bounces.clear( );
		out.valid = false;
		out.end_tick = -1;

		auto pos = start;
		auto vel = velocity;
		auto bounce_count{ 0 };
		auto tick_timer{ 0 };

		for ( int tick = 0; tick < max_ticks; ++tick )
		{
			if ( tick_timer == 0 )
			{
				out.points.push_back( pos );
			}

			systems::bvh::trace_result trace{};
			auto impact_detonate{ false };

			this->step_simulation( pos, vel, trace, impact_detonate );

			if ( trace.hit )
			{
				++bounce_count;
				out.bounces.push_back( pos );
			}

			const auto velocity_stopped = std::fabsf( vel.x ) < 20.0f && std::fabsf( vel.y ) < 20.0f && vel.length_sqr( ) < k_stop_speed_sq;
			if ( impact_detonate || this->should_detonate( vel, tick ) || bounce_count > 20 || velocity_stopped )
			{
				out.end_tick = tick;
				out.end_pos = pos;
				out.duration = static_cast< float >( tick ) * cstypes::tick_interval;
				break;
			}

			if ( trace.hit || ++tick_timer >= ticks_per_point )
			{
				tick_timer = 0;
			}
		}

		if ( !out.points.empty( ) && out.end_tick >= 0 )
		{
			if ( out.points.back( ).distance_sqr( out.end_pos ) > 1.0f )
			{
				out.points.push_back( out.end_pos );
			}
		}

		out.valid = out.end_tick >= 0;
	}

	void grenades::step_simulation( math::vector3& pos, math::vector3& vel, systems::bvh::trace_result& trace, bool& detonated )
	{
		detonated = false;

		const auto gravity = this->m_sv_gravity * gravity_scale;
		const auto new_vel_z = vel.z - gravity * cstypes::tick_interval;

		const math::vector3 move
		{
			vel.x * cstypes::tick_interval,
			vel.y * cstypes::tick_interval,
			( vel.z + new_vel_z ) * 0.5f * cstypes::tick_interval
		};

		vel.z = new_vel_z;

		const auto hull_mins = math::vector3{ -k_hull_size, -k_hull_size, -k_hull_size };
		const auto hull_maxs = math::vector3{ k_hull_size, k_hull_size, k_hull_size };
		trace = systems::g_bvh.trace_hull( pos, pos + move, hull_mins, hull_maxs );
		pos = trace.end_pos;

		if ( trace.hit )
		{
			this->resolve_collision( trace, pos, vel, detonated );
		}
	}

	void grenades::resolve_collision( const systems::bvh::trace_result& trace, math::vector3& pos, math::vector3& vel, bool& detonated )
	{
		detonated = false;

		const auto is_molotov = this->m_weapon_hash == "weapon_molotov"_hash || this->m_weapon_hash == "weapon_incgrenade"_hash;
		if ( is_molotov )
		{
			if ( trace.normal.z >= this->m_molotov_max_slope_z || vel.length_sqr( ) < k_stop_speed_sq )
			{
				detonated = true;
				vel = {};
				return;
			}
		}

		auto new_vel = clip_velocity( vel, trace.normal, 2.0f );

		new_vel.x *= elasticity;
		new_vel.y *= elasticity;
		new_vel.z *= elasticity;

		if ( trace.normal.z > k_steep_bounce_normal_z )
		{
			const auto speed_sqr = new_vel.length_sqr( );

			if ( speed_sqr > k_steep_bounce_speed_sq )
			{
				const auto l = new_vel.normalized( ).dot( trace.normal );
				if ( l > 0.5f )
				{
					const auto dampen = 1.5f - l;
					new_vel.x *= dampen;
					new_vel.y *= dampen;
					new_vel.z *= dampen;
				}
			}

			if ( new_vel.length_sqr( ) < k_stop_speed_sq )
			{
				vel = {};
				return;
			}
		}

		vel = new_vel;

		const auto remaining = 1.0f - trace.fraction;
		if ( remaining > 0.0f )
		{
			const auto hull_mins = math::vector3{ -k_hull_size, -k_hull_size, -k_hull_size };
			const auto hull_maxs = math::vector3{ k_hull_size, k_hull_size, k_hull_size };
			const auto post_trace = systems::g_bvh.trace_hull( pos, pos + new_vel * ( remaining * cstypes::tick_interval ), hull_mins, hull_maxs );
			pos = post_trace.end_pos;
		}
	}

	bool grenades::should_detonate( const math::vector3& vel, int tick ) const
	{
		switch ( this->m_weapon_hash )
		{
		case "weapon_smokegrenade"_hash:
		case "weapon_decoy"_hash:
		{
			const auto speed_2d = std::sqrtf( vel.x * vel.x + vel.y * vel.y );
			const auto check_ticks = static_cast< int >( 0.2f / cstypes::tick_interval );
			return speed_2d < this->m_velocity_threshold && ( tick % check_ticks ) == 0;
		}

		case "weapon_molotov"_hash:
		case "weapon_incgrenade"_hash:
			return static_cast< float >( tick ) * cstypes::tick_interval > this->m_detonate_time;

		case "weapon_flashbang"_hash:
		case "weapon_hegrenade"_hash:
			return static_cast< float >( tick - 8 ) * cstypes::tick_interval > this->m_detonate_time;

		default:
			return false;
		}
	}

	math::vector3 grenades::clip_velocity( const math::vector3& velocity, const math::vector3& normal, float overbounce )
	{
		const auto backoff = velocity.dot( normal ) * overbounce;

		math::vector3 out
		{
			velocity.x - normal.x * backoff,
			velocity.y - normal.y * backoff,
			velocity.z - normal.z * backoff
		};

		if ( std::fabsf( out.x ) < 0.1f )
		{
			out.x = 0.0f;
		}

		if ( std::fabsf( out.y ) < 0.1f )
		{
			out.y = 0.0f;
		}

		if ( std::fabsf( out.z ) < 0.1f )
		{
			out.z = 0.0f;
		}

		return out;
	}

	void grenades::render_trajectory( zdraw::draw_list& draw_list, const trajectory& traj, float alpha ) const
	{
		if ( !traj.valid || traj.points.size( ) < 2 )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_grenades;
		const auto total = traj.points.size( );

		for ( std::size_t i = 0; i + 1 < total; ++i )
		{
			const auto s0 = systems::g_view.project( traj.points[ i ] );
			const auto s1 = systems::g_view.project( traj.points[ i + 1 ] );

			if ( !systems::g_view.projection_valid( s0 ) || !systems::g_view.projection_valid( s1 ) )
			{
				continue;
			}

			const auto t = static_cast< float >( i ) / static_cast< float >( total - 1 );
			const auto seg_alpha = alpha * ( 1.0f - t * 0.6f );
			const auto a = static_cast< std::uint8_t >( std::clamp( seg_alpha * static_cast< float >( cfg.color.a ), 0.0f, 255.0f ) );

			draw_list.add_line( s0.x, s0.y, s1.x, s1.y, { cfg.color.r, cfg.color.g, cfg.color.b, a }, 2.0f );
		}

		for ( const auto& bounce : traj.bounces )
		{
			const auto s = systems::g_view.project( bounce );
			if ( !systems::g_view.projection_valid( s ) )
			{
				continue;
			}

			const auto a = static_cast< std::uint8_t >( alpha * static_cast< float >( cfg.color.a ) );

			draw_list.add_rect_filled( s.x - 3.0f, s.y - 3.0f, 6.0f, 6.0f, { 0, 0, 0, a } );
			draw_list.add_rect_filled( s.x - 2.0f, s.y - 2.0f, 4.0f, 4.0f, { cfg.color.r, cfg.color.g, cfg.color.b, a } );
		}

		if ( traj.end_tick >= 0 )
		{
			const auto s = systems::g_view.project( traj.end_pos );
			if ( systems::g_view.projection_valid( s ) )
			{
				const auto a = static_cast< std::uint8_t >( alpha * static_cast< float >( cfg.color.a ) );

				draw_list.add_rect_filled( s.x - 5.0f, s.y - 5.0f, 10.0f, 10.0f, { 0, 0, 0, a } );
				draw_list.add_rect_filled( s.x - 4.0f, s.y - 4.0f, 8.0f, 8.0f, { cfg.color.r, cfg.color.g, cfg.color.b, a } );
			}
		}
	}

    struct lineup
    {
        std::string map;
        std::string name;
        std::string grenade;
        math::vector3 pos;
        math::vector3 aim_angles;
        int throw_type;
    };

    static std::vector<lineup> g_lineups;
    static bool g_lineups_loaded = false;

    // Helper parser for lineups.json
    static std::vector<lineup> parse_lineups(const std::string& filepath)
    {
        std::vector<lineup> list;
        std::ifstream file(filepath);
        if (!file.is_open())
            return list;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();

        size_t pos = 0;
        while (true)
        {
            size_t start = content.find('{', pos);
            if (start == std::string::npos)
                break;
            size_t end = content.find('}', start);
            if (end == std::string::npos)
                break;

            std::string obj = content.substr(start, end - start);
            pos = end + 1;

            lineup item;

            auto get_str_val = [&](const std::string& key) -> std::string {
                size_t k = obj.find("\"" + key + "\"");
                if (k == std::string::npos) return "";
                size_t v_start = obj.find('"', k + key.size() + 2);
                if (v_start == std::string::npos) return "";
                size_t v_end = obj.find('"', v_start + 1);
                if (v_end == std::string::npos) return "";
                return obj.substr(v_start + 1, v_end - (v_start + 1));
            };

            auto get_vec_val = [&](const std::string& key) -> math::vector3 {
                size_t k = obj.find("\"" + key + "\"");
                if (k == std::string::npos) return {0,0,0};
                size_t arr_start = obj.find('[', k);
                if (arr_start == std::string::npos) return {0,0,0};
                size_t arr_end = obj.find(']', arr_start);
                if (arr_end == std::string::npos) return {0,0,0};
                std::string arr_str = obj.substr(arr_start + 1, arr_end - (arr_start + 1));

                math::vector3 vec{0,0,0};
                size_t c1 = arr_str.find(',');
                if (c1 != std::string::npos)
                {
                    vec.x = std::stof(arr_str.substr(0, c1));
                    size_t c2 = arr_str.find(',', c1 + 1);
                    if (c2 != std::string::npos)
                    {
                        vec.y = std::stof(arr_str.substr(c1 + 1, c2 - (c1 + 1)));
                        vec.z = std::stof(arr_str.substr(c2 + 1));
                    }
                }
                return vec;
            };

            auto get_int_val = [&](const std::string& key) -> int {
                size_t k = obj.find("\"" + key + "\"");
                if (k == std::string::npos) return 0;
                size_t val_start = obj.find(':', k);
                if (val_start == std::string::npos) return 0;
                size_t num_start = obj.find_first_of("-0123456789", val_start);
                if (num_start == std::string::npos) return 0;
                size_t num_end = obj.find_first_not_of("0123456789", num_start);
                return std::stoi(obj.substr(num_start, num_end - num_start));
            };

            item.map = get_str_val("map");
            item.name = get_str_val("name");
            item.grenade = get_str_val("grenade");
            item.pos = get_vec_val("pos");
            item.aim_angles = get_vec_val("aim_angles");
            item.throw_type = get_int_val("throw_type");

            list.push_back(item);
        }
        return list;
    }

    static std::string find_lineups_path()
    {
        wchar_t doc_path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, doc_path)))
        {
            std::wstring path = std::wstring(doc_path) + L"\\Aero\\lineups.json";
            if (std::filesystem::exists(path))
                return std::string(path.begin(), path.end());
        }

        wchar_t exe_path[MAX_PATH];
        if (GetModuleFileNameW(NULL, exe_path, MAX_PATH))
        {
            PathRemoveFileSpecW(exe_path);
            std::wstring path = std::wstring(exe_path) + L"\\lineup_data\\lineups.json";
            if (std::filesystem::exists(path))
                return std::string(path.begin(), path.end());
        }

        if (std::filesystem::exists("lineup_data\\lineups.json"))
            return "lineup_data\\lineups.json";

        if (std::filesystem::exists("c:\\Users\\tymek\\Desktop\\catalyst src\\lineup_data\\lineups.json"))
            return "c:\\Users\\tymek\\Desktop\\catalyst src\\lineup_data\\lineups.json";

        return "";
    }

    static void draw_3d_circle(zdraw::draw_list& draw_list, const math::vector3& center, float radius, zdraw::rgba color)
    {
        constexpr int points_count = 32;
        std::vector<math::vector2> screen_points;
        screen_points.reserve(points_count);

        for (int i = 0; i < points_count; ++i)
        {
            float angle = (i * 2.0f * std::numbers::pi_v<float>) / points_count;
            math::vector3 point = center + math::vector3{ std::cos(angle) * radius, std::sin(angle) * radius, 0.0f };
            math::vector2 screen = systems::g_view.project(point);
            if (systems::g_view.projection_valid(screen))
            {
                screen_points.push_back(screen);
            }
        }

        if (screen_points.size() >= 2)
        {
            for (size_t i = 0; i < screen_points.size(); ++i)
            {
                size_t next = (i + 1) % screen_points.size();
                draw_list.add_line(screen_points[i].x, screen_points[i].y, screen_points[next].x, screen_points[next].y, color, 1.5f);
            }
        }
    }

    static math::vector3 angle_to_direction(const math::vector3& angles)
    {
        float pitch_rad = angles.x * std::numbers::pi_v<float> / 180.0f;
        float yaw_rad = angles.y * std::numbers::pi_v<float> / 180.0f;

        float cp = std::cos(pitch_rad);
        float sp = std::sin(pitch_rad);
        float cy = std::cos(yaw_rad);
        float sy = std::sin(yaw_rad);

        return math::vector3{ cp * cy, cp * sy, -sp };
    }

	void nade_helper::on_render( zdraw::draw_list& draw_list )
	{
		const auto& cfg = settings::g_misc.m_nade_helper;
		if ( !cfg.enabled )
		{
			return;
		}

		if ( !systems::g_local.valid( ) )
		{
			return;
		}

		const auto local_pawn = systems::g_local.pawn();
		if (!local_pawn)
		{
			return;
		}

		// Only activate when holding a grenade
		if (systems::g_local.weapon_type() != cstypes::weapon_type::grenade)
		{
			return;
		}

		if (!g_lineups_loaded)
		{
			std::string path = find_lineups_path();
			if (!path.empty())
			{
				g_lineups = parse_lineups(path);
			}
			g_lineups_loaded = true;
		}

		if (g_lineups.empty())
		{
			return;
		}

		// Get current map
		std::string current_map = "";
		const auto global_vars = g::memory.read<std::uintptr_t>( g::offsets.global_vars );
		if (global_vars)
		{
			const auto map_ptr = g::memory.read<std::uintptr_t>( global_vars + 0x188 );
			if (map_ptr)
			{
				current_map = g::memory.read_string( map_ptr );
			}
		}

		if (current_map.empty())
		{
			return;
		}

		// Get current grenade type
		const auto vdata = systems::g_local.weapon_vdata();
		std::string grenade_type = "";
		if (vdata)
		{
			const auto name_ptr = g::memory.read<std::uintptr_t>( vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_hash ) );
			if (name_ptr)
			{
				std::string weapon_name = g::memory.read_string( name_ptr, 64 );
				if (weapon_name.starts_with( "weapon_" ))
					weapon_name.erase( 0, 7 );
				
				if (weapon_name == "smokegrenade") grenade_type = "smoke";
				else if (weapon_name == "flashbang") grenade_type = "flash";
				else if (weapon_name == "molotov" || weapon_name == "incgrenade") grenade_type = "molotov";
				else if (weapon_name == "hegrenade") grenade_type = "he";
			}
		}

		if (grenade_type.empty())
		{
			return;
		}

		// Get local player origin
		const auto game_scene_node = g::memory.read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if (!game_scene_node)
		{
			return;
		}
		const auto local_origin = g::memory.read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

		const auto eye_pos = systems::g_view.origin();

		for (const auto& lineup : g_lineups)
		{
			if (lineup.map != current_map || lineup.grenade != grenade_type)
				continue;

			float dist = local_origin.distance(lineup.pos);

			// Render stands within 2000 units (approx 50 meters)
			if (dist < 2000.0f)
			{
				bool inside = dist < 20.0f;
				zdraw::rgba circle_color = inside ? zdraw::rgba{ 50, 220, 100, 220 } : zdraw::rgba{ 220, 150, 50, 180 };

				// Draw 3D circle at standing position
				draw_3d_circle(draw_list, lineup.pos, 15.0f, circle_color);

				if (inside)
				{
					// Draw aim target marker
					math::vector3 target_3d = eye_pos + angle_to_direction(lineup.aim_angles) * 100.0f;
					math::vector2 target_screen = systems::g_view.project(target_3d);

					if (systems::g_view.projection_valid(target_screen))
					{
						// Draw aim target crosshair / dot
						draw_list.add_circle(target_screen.x, target_screen.y, 6.0f, zdraw::rgba{ 50, 220, 100, 255 }, 12, 1.5f);
						draw_list.add_circle_filled(target_screen.x, target_screen.y, 2.0f, zdraw::rgba{ 255, 255, 255, 255 });

						// Display lineup name
						std::string text = lineup.name;
						float tw{ 0.f }, th{ 0.f };
						const auto fnt = zdraw::get_font();
						if (fnt)
						{
							fnt->calc_text_size(text, tw, th);
						}
						draw_list.add_text(target_screen.x - tw / 2.f, target_screen.y + 12.f, text, nullptr, zdraw::rgba{ 255, 255, 255, 255 }, zdraw::text_style::outlined);

						// Display throw type
						std::string throw_str = "Normal Throw";
						if (lineup.throw_type == 1) throw_str = "Jump Throw";
						else if (lineup.throw_type == 2) throw_str = "Walk Throw";
						else if (lineup.throw_type == 3) throw_str = "Right Click Throw";
						
						float ttw{ 0.f }, tth{ 0.f };
						if (fnt)
						{
							fnt->calc_text_size(throw_str, ttw, tth);
						}
						draw_list.add_text(target_screen.x - ttw / 2.f, target_screen.y + 26.f, throw_str, nullptr, zdraw::rgba{ 180, 220, 255, 225 }, zdraw::text_style::outlined);
					}
				}
				else
				{
					// If not inside, show standing point name on screen
					math::vector2 screen_pos = systems::g_view.project(lineup.pos + math::vector3{ 0.0f, 0.0f, 10.0f });
					if (systems::g_view.projection_valid(screen_pos))
					{
						std::string text = lineup.name + " (" + std::to_string((int)(dist * 0.0254f)) + "m)";
						float tw{ 0.f }, th{ 0.f };
						const auto fnt = zdraw::get_font();
						if (fnt)
						{
							fnt->calc_text_size(text, tw, th);
						}
						draw_list.add_text(screen_pos.x - tw / 2.f, screen_pos.y, text, nullptr, zdraw::rgba{ 220, 180, 100, 220 }, zdraw::text_style::outlined);
					}
				}
			}
		}
	}

    struct map_radar_config
    {
        std::string map_name;
        float pos_x{0.f};
        float pos_y{0.f};
        float scale{1.f};
    };

    static std::vector<map_radar_config> g_radar_configs;
    static bool g_radar_configs_loaded = false;

    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_radar_texture = nullptr;
    static std::string g_loaded_radar_map = "";
    static map_radar_config g_current_radar_cfg;

    static std::vector<map_radar_config> parse_radar_configs(const std::string& filepath)
    {
        std::vector<map_radar_config> list;
        std::ifstream file(filepath);
        if (!file.is_open())
            return list;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();

        size_t pos = 0;
        while (true)
        {
            size_t start = content.find('{', pos);
            if (start == std::string::npos)
                break;
            size_t end = content.find('}', start);
            if (end == std::string::npos)
                break;

            std::string obj = content.substr(start, end - start);
            pos = end + 1;

            map_radar_config item;

            auto get_str_val = [&](const std::string& key) -> std::string {
                size_t k = obj.find("\"" + key + "\"");
                if (k == std::string::npos) return "";
                size_t v_start = obj.find('"', k + key.size() + 2);
                if (v_start == std::string::npos) return "";
                size_t v_end = obj.find('"', v_start + 1);
                if (v_end == std::string::npos) return "";
                return obj.substr(v_start + 1, v_end - (v_start + 1));
            };

            auto get_float_val = [&](const std::string& key) -> float {
                size_t k = obj.find("\"" + key + "\"");
                if (k == std::string::npos) return 0.0f;
                size_t val_start = obj.find(':', k);
                if (val_start == std::string::npos) return 0.0f;
                size_t num_start = obj.find_first_of("-0123456789.", val_start);
                if (num_start == std::string::npos) return 0.0f;
                size_t num_end = obj.find_first_not_of("0123456789.-", num_start);
                return std::stof(obj.substr(num_start, num_end - num_start));
            };

            item.map_name = get_str_val("map_name");
            if (item.map_name.empty()) continue;

            item.pos_x = get_float_val("pos_x");
            item.pos_y = get_float_val("pos_y");
            item.scale = get_float_val("scale");

            list.push_back(item);
        }
        return list;
    }

    static std::string find_radar_folder()
    {
        wchar_t exe_path[MAX_PATH];
        if (GetModuleFileNameW(NULL, exe_path, MAX_PATH))
        {
            PathRemoveFileSpecW(exe_path);
            std::wstring path = std::wstring(exe_path) + L"\\radar";
            if (std::filesystem::exists(path))
                return std::string(path.begin(), path.end()) + "\\";
        }

        if (std::filesystem::exists("radar"))
            return "radar\\";

        if (std::filesystem::exists("c:\\Users\\tymek\\Desktop\\catalyst src\\radar"))
            return "c:\\Users\\tymek\\Desktop\\catalyst src\\radar\\";

        return "";
    }

    void radar::on_render(zdraw::draw_list& draw_list)
    {
        const auto& cfg = settings::g_misc.m_radar;
        if (!cfg.enabled)
            return;

        if (!systems::g_local.valid())
            return;

        const auto local_pawn = systems::g_local.pawn();
        const auto local_team = systems::g_local.team();
        const auto players = systems::g_collector.players();

        // Get current map name
        std::string current_map = "";
        const auto global_vars = g::memory.read<std::uintptr_t>(g::offsets.global_vars);
        if (global_vars)
        {
            const auto map_ptr = g::memory.read<std::uintptr_t>(global_vars + 0x188);
            if (map_ptr)
            {
                current_map = g::memory.read_string(map_ptr);
            }
        }

        if (current_map.empty())
            return;

        std::string radar_folder = find_radar_folder();

        // Load configs once
        if (!g_radar_configs_loaded && !radar_folder.empty())
        {
            g_radar_configs = parse_radar_configs(radar_folder + "radar_config.json");
            g_radar_configs_loaded = true;
        }

        // Handle texture loading on map change
        if (current_map != g_loaded_radar_map)
        {
            g_radar_texture = nullptr;
            g_loaded_radar_map = current_map;

            // Find config
            bool found_config = false;
            for (const auto& c : g_radar_configs)
            {
                if (c.map_name == current_map)
                {
                    g_current_radar_cfg = c;
                    found_config = true;
                    break;
                }
            }

            // Fallback to default config
            if (!found_config)
            {
                for (const auto& c : g_radar_configs)
                {
                    if (c.map_name == "default")
                    {
                        g_current_radar_cfg = c;
                        found_config = true;
                        break;
                    }
                }
            }

            if (!found_config)
            {
                g_current_radar_cfg = { current_map, 0.0f, 0.0f, 1.0f };
            }

            // Load texture
            if (!radar_folder.empty())
            {
                std::string image_path = radar_folder + current_map + ".png";
                if (std::filesystem::exists(image_path))
                {
                    g_radar_texture = zdraw::load_texture_from_file(image_path);
                }
                
                if (!g_radar_texture)
                {
                    std::string default_path = radar_folder + "default.png";
                    if (std::filesystem::exists(default_path))
                    {
                        g_radar_texture = zdraw::load_texture_from_file(default_path);
                    }
                }
            }
        }

        // Draw radar box
        float x = cfg.pos_x;
        float y = cfg.pos_y;
        float size = cfg.size;

        // Background with configurable opacity
        std::uint8_t background_alpha = static_cast<std::uint8_t>(cfg.opacity * 180.0f);
        draw_list.add_rect_filled(x, y, size, size, zdraw::rgba{ 15, 15, 15, background_alpha });

        // Map Overview texture with configurable opacity
        if (g_radar_texture)
        {
            std::uint8_t texture_alpha = static_cast<std::uint8_t>(cfg.opacity * 255.0f);
            draw_list.add_rect_textured(x, y, size, size, g_radar_texture.Get(), 0.0f, 0.0f, 1.0f, 1.0f, zdraw::rgba{ 255, 255, 255, texture_alpha });
        }

        // Draw border
        draw_list.add_rect(x, y, size, size, zdraw::rgba{ 80, 80, 80, 255 }, 1.5f);

        // Title text
        std::string title = "Radar - " + current_map;
        draw_list.add_text(x + 5.0f, y - 18.0f, title, nullptr, zdraw::rgba{ 255, 255, 255, 255 }, zdraw::text_style::outlined);

        // Local player origin
        math::vector3 local_origin{ 0.0f, 0.0f, 0.0f };
        if (local_pawn)
        {
            const auto game_scene_node = g::memory.read<std::uintptr_t>(local_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
            if (game_scene_node)
            {
                local_origin = g::memory.read<math::vector3>(game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
            }
        }

        // Draw local player dot
        if (local_origin.x != 0.0f || local_origin.y != 0.0f)
        {
            float map_x = (local_origin.x - g_current_radar_cfg.pos_x) / g_current_radar_cfg.scale;
            float map_y = (g_current_radar_cfg.pos_y - local_origin.y) / g_current_radar_cfg.scale;

            float norm_x = map_x / 1024.0f;
            float norm_y = map_y / 1024.0f;

            float screen_x = x + norm_x * size;
            float screen_y = y + norm_y * size;

            if (screen_x >= x && screen_x <= x + size && screen_y >= y && screen_y <= y + size)
            {
                // Draw local dot (white)
                draw_list.add_circle_filled(screen_x, screen_y, 4.0f, zdraw::rgba{ 255, 255, 255, 255 });
                draw_list.add_circle(screen_x, screen_y, 4.0f, zdraw::rgba{ 0, 0, 0, 255 }, 12, 1.0f);

                // Local player direction line
                float yaw = systems::g_view.angles().y;
                float yaw_rad = -yaw * (std::numbers::pi_v<float> / 180.0f);
                float dir_x = screen_x + std::cos(yaw_rad) * 12.0f;
                float dir_y = screen_y + std::sin(yaw_rad) * 12.0f;
                draw_list.add_line(screen_x, screen_y, dir_x, dir_y, zdraw::rgba{ 255, 255, 255, 255 }, 1.5f);
            }
        }

        // Draw other players
        for (const auto& player : players)
        {
            if (player.pawn == local_pawn || player.health <= 0)
                continue;

            bool is_enemy = systems::g_local.is_enemy(player.team);
            if (is_enemy && !cfg.show_enemies)
                continue;
            if (!is_enemy && !cfg.show_teammates)
                continue;

            float map_x = (player.origin.x - g_current_radar_cfg.pos_x) / g_current_radar_cfg.scale;
            float map_y = (g_current_radar_cfg.pos_y - player.origin.y) / g_current_radar_cfg.scale;

            float norm_x = map_x / 1024.0f;
            float norm_y = map_y / 1024.0f;

            float screen_x = x + norm_x * size;
            float screen_y = y + norm_y * size;

            // Clamp to boundaries if out of bounds (so player sees where they are even off map)
            float clamped_x = std::clamp(screen_x, x + 4.0f, x + size - 4.0f);
            float clamped_y = std::clamp(screen_y, y + 4.0f, y + size - 4.0f);

            zdraw::rgba color = is_enemy ? zdraw::rgba{ 255, 60, 60, 255 } : zdraw::rgba{ 60, 180, 255, 255 };

            draw_list.add_circle_filled(clamped_x, clamped_y, 4.0f, color);
            draw_list.add_circle(clamped_x, clamped_y, 4.0f, zdraw::rgba{ 0, 0, 0, 255 }, 12, 1.0f);
        }
    }

    void visual_extras::register_hit()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_hit_time = std::chrono::steady_clock::now();
    }

    void visual_extras::add_damage_indicator(int damage, const math::vector3& pos)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_damage_indicators.push_back({ damage, pos, std::chrono::steady_clock::now() });
    }

    void visual_extras::on_render(zdraw::draw_list& draw_list)
    {
        if (settings::g_misc.m_spectator_list.enabled)
        {
            this->draw_spectator_list(draw_list);
        }

        if (settings::g_misc.m_bomb_timer.enabled)
        {
            this->draw_bomb_timer(draw_list);
        }

        if (settings::g_misc.m_hitmarker.enabled)
        {
            this->draw_hitmarker(draw_list);
        }

        if (settings::g_misc.m_damage_indicator.enabled)
        {
            this->draw_damage_indicators(draw_list);
        }
    }

    void visual_extras::draw_spectator_list(zdraw::draw_list& draw_list)
    {
        if (!systems::g_local.valid())
            return;

        const auto local_controller = systems::g_local.controller();
        const auto local_view_pawn = systems::g_local.view_pawn();
        if (!local_controller || !local_view_pawn)
            return;

        // Query CCSPlayerController entities directly
        const auto entities = systems::g_entities.by_type(systems::entities::type::player);
        std::vector<std::string> spectators;

        for (const auto& entry : entities)
        {
            if (entry.ptr == local_controller)
                continue;

            // Alive players cannot spectate, skip them
            const auto pawn_handle = g::memory.read<std::uint32_t>(entry.ptr + SCHEMA("CCSPlayerController", "m_hPlayerPawn"_hash));
            if (pawn_handle && pawn_handle != 0xffffffff)
            {
                const auto player_pawn = systems::g_entities.lookup(pawn_handle);
                if (player_pawn)
                {
                    int health = g::memory.read<std::int32_t>(player_pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
                    if (health > 0)
                    {
                        continue;
                    }
                }
            }

            std::uintptr_t observer_pawn = 0;

            // Try observer pawn first
            const auto obs_handle = g::memory.read<std::uint32_t>(entry.ptr + SCHEMA("CCSPlayerController", "m_hObserverPawn"_hash));
            if (obs_handle && obs_handle != 0xffffffff)
            {
                observer_pawn = systems::g_entities.lookup(obs_handle);
            }

            // Fallback to player pawn
            if (!observer_pawn)
            {
                const auto pawn_handle = g::memory.read<std::uint32_t>(entry.ptr + SCHEMA("CCSPlayerController", "m_hPlayerPawn"_hash));
                if (pawn_handle && pawn_handle != 0xffffffff)
                {
                    observer_pawn = systems::g_entities.lookup(pawn_handle);
                }
            }

            if (observer_pawn)
            {
                const auto observer_services = g::memory.read<std::uintptr_t>(observer_pawn + SCHEMA("C_BasePlayerPawn", "m_pObserverServices"_hash));
                if (observer_services)
                {
                    const auto target_handle = g::memory.read<std::uint32_t>(observer_services + SCHEMA("CPlayer_ObserverServices", "m_hObserverTarget"_hash));
                    if (target_handle && target_handle != 0xffffffff)
                    {
                        const auto target_pawn = systems::g_entities.lookup(target_handle);
                        if (target_pawn && target_pawn == local_view_pawn)
                        {
                            const auto name_ptr = g::memory.read<std::uintptr_t>(entry.ptr + SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash));
                            if (name_ptr)
                            {
                                std::string name = g::memory.read_string(name_ptr, 128);
                                if (!name.empty())
                                {
                                    spectators.push_back(name);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (spectators.empty())
            return;

        float x = settings::g_misc.m_spectator_list.pos_x;
        float y = settings::g_misc.m_spectator_list.pos_y;
        float width = 180.0f;
        float header_height = 20.0f;
        float item_height = 18.0f;
        float total_height = header_height + (spectators.size() * item_height) + 5.0f;

        draw_list.add_rect_filled(x, y, width, total_height, zdraw::rgba{ 15, 15, 15, 180 });
        draw_list.add_rect(x, y, width, total_height, zdraw::rgba{ 80, 80, 80, 255 }, 1.0f);

        std::string header = "Spectators (" + std::to_string(spectators.size()) + ")";
        draw_list.add_text(x + 8.0f, y + 2.0f, header, nullptr, zdraw::rgba{ 180, 220, 255, 255 }, zdraw::text_style::outlined);

        float current_y = y + header_height + 2.0f;
        for (const auto& name : spectators)
        {
            draw_list.add_text(x + 10.0f, current_y, name, nullptr, zdraw::rgba{ 255, 255, 255, 240 }, zdraw::text_style::outlined);
            current_y += item_height;
        }
    }

    void visual_extras::draw_bomb_timer(zdraw::draw_list& draw_list)
    {
        std::uintptr_t planted_c4 = 0;
        const auto items = systems::g_entities.by_type(systems::entities::type::item);
        for (const auto& item : items)
        {
            if (item.schema_hash == fnv1a::runtime_hash("C_PlantedC4"))
            {
                planted_c4 = item.ptr;
                break;
            }
        }

        if (!planted_c4)
            return;

        bool ticking = g::memory.read<bool>(planted_c4 + SCHEMA("C_PlantedC4", "m_bBombTicking"_hash));
        bool defused = g::memory.read<bool>(planted_c4 + SCHEMA("C_PlantedC4", "m_bBombDefused"_hash));

        if (!ticking || defused)
            return;

        const auto global_vars = g::memory.read<std::uintptr_t>(g::offsets.global_vars);
        if (!global_vars)
            return;

        float time = g::memory.read<float>(global_vars + 0x30);
        float blow_time = g::memory.read<float>(planted_c4 + SCHEMA("C_PlantedC4", "m_flC4Blow"_hash));
        float timer_length = g::memory.read<float>(planted_c4 + SCHEMA("C_PlantedC4", "m_flTimerLength"_hash));

        float time_left = blow_time - time;
        if (time_left <= 0.0f || time_left > 45.0f)
            return;

        float screen_w = ::GetSystemMetrics(SM_CXSCREEN);
        float screen_h = ::GetSystemMetrics(SM_CYSCREEN);

        // Calculate site and damage
        int bomb_site = g::memory.read<int>(planted_c4 + SCHEMA("C_PlantedC4", "m_nBombSite"_hash));
        std::string site_str = (bomb_site == 0) ? "A" : ((bomb_site == 1) ? "B" : std::to_string(bomb_site));

        int dmg = 0;
        const auto local_pawn = systems::g_local.pawn();
        if (local_pawn)
        {
            int local_health = g::memory.read<std::int32_t>(local_pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
            if (local_health > 0)
            {
                int local_armor = g::memory.read<std::int32_t>(local_pawn + SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));
                
                math::vector3 local_origin{};
                const auto local_game_scene = g::memory.read<std::uintptr_t>(local_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
                if (local_game_scene)
                {
                    local_origin = g::memory.read<math::vector3>(local_game_scene + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
                }

                math::vector3 bomb_origin{};
                const auto bomb_game_scene = g::memory.read<std::uintptr_t>(planted_c4 + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
                if (bomb_game_scene)
                {
                    bomb_origin = g::memory.read<math::vector3>(bomb_game_scene + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
                }

                float distance = local_origin.distance(bomb_origin);
                float base_damage = g::memory.read<float>(planted_c4 + SCHEMA("C_PlantedC4", "m_flDamage"_hash));
                if (base_damage <= 0.0f) base_damage = 500.0f;

                float radius = g::memory.read<float>(planted_c4 + SCHEMA("C_PlantedC4", "m_flRadius"_hash));
                if (radius <= 0.0f) radius = base_damage * 3.5f;

                if (distance < radius)
                {
                    float sigma = radius * 0.35f;
                    float falloff = base_damage * std::exp(-distance * distance / (2.0f * sigma * sigma));
                    
                    float final_damage = falloff;
                    if (local_armor > 0)
                    {
                        float armor_ratio = 0.5f;
                        float armor_bonus = 0.5f;
                        
                        float new_damage = final_damage * armor_ratio;
                        float armor_to_drop = (final_damage - new_damage) * armor_bonus;
                        
                        if (armor_to_drop > (float)local_armor)
                        {
                            armor_to_drop = (float)local_armor * (1.0f / armor_bonus);
                            new_damage = final_damage - armor_to_drop;
                        }
                        final_damage = new_damage;
                    }
                    dmg = static_cast<int>(std::round(final_damage));
                }
            }
        }

        char timer_buf[128];
        const auto local_pawn_p = systems::g_local.pawn();
        int local_h = local_pawn_p ? g::memory.read<std::int32_t>(local_pawn_p + SCHEMA("C_BaseEntity", "m_iHealth"_hash)) : 0;
        
        if (local_pawn_p && local_h > 0)
        {
            if (dmg >= local_h)
            {
                sprintf_s(timer_buf, "C4 (Site %s): %.2fs | FATAL (-%d HP)", site_str.c_str(), time_left, dmg);
            }
            else if (dmg > 0)
            {
                sprintf_s(timer_buf, "C4 (Site %s): %.2fs | Damage: -%d HP", site_str.c_str(), time_left, dmg);
            }
            else
            {
                sprintf_s(timer_buf, "C4 (Site %s): %.2fs | Out of range", site_str.c_str(), time_left);
            }
        }
        else
        {
            sprintf_s(timer_buf, "C4 (Site %s): %.2fs", site_str.c_str(), time_left);
        }

        float bar_width = 300.0f;
        float bar_height = 8.0f;
        float x = (screen_w - bar_width) / 2.0f;
        float y = 60.0f;

        float tw = 0.0f, th = 0.0f;
        const auto fnt = zdraw::get_font();
        if (fnt)
        {
            fnt->calc_text_size(timer_buf, tw, th);
        }

        float container_width = std::max(bar_width, tw + 20.0f);
        float container_x = (screen_w - container_width) / 2.0f;

        // Draw background
        draw_list.add_rect_filled(container_x, y - 20.0f, container_width, bar_height + 25.0f, zdraw::rgba{ 15, 15, 15, 180 });
        draw_list.add_rect(container_x, y - 20.0f, container_width, bar_height + 25.0f, zdraw::rgba{ 80, 80, 80, 255 }, 1.0f);

        // Draw C4 Timer Bar
        float progress = std::clamp(time_left / timer_length, 0.0f, 1.0f);
        zdraw::rgba bar_color = time_left > 10.0f ? zdraw::rgba{ 220, 180, 60, 255 } : zdraw::rgba{ 255, 60, 60, 255 };
        draw_list.add_rect_filled(x, y, bar_width * progress, bar_height, bar_color);
        draw_list.add_rect(x, y, bar_width, bar_height, zdraw::rgba{ 40, 40, 40, 255 }, 1.0f);

        draw_list.add_text((screen_w - tw) / 2.0f, y - 18.0f, timer_buf, nullptr, zdraw::rgba{ 255, 255, 255, 255 }, zdraw::text_style::outlined);

        // Draw Defuse progress bar
        const auto defuser = g::memory.read<std::uint32_t>(planted_c4 + SCHEMA("C_PlantedC4", "m_hBombDefuser"_hash));
        if (defuser != 0 && defuser != 0xffffffff)
        {
            float defuse_time = g::memory.read<float>(planted_c4 + SCHEMA("C_PlantedC4", "m_flDefuseCountDown"_hash));
            float defuse_length = g::memory.read<float>(planted_c4 + SCHEMA("C_PlantedC4", "m_flDefuseLength"_hash));

            float defuse_left = defuse_time - time;
            if (defuse_left > 0.0f)
            {
                float defuse_y = y + bar_height + 8.0f;
                
                // Resize container dynamically to fit second bar
                draw_list.add_rect_filled(container_x, defuse_y - 2.0f, container_width, bar_height + 10.0f, zdraw::rgba{ 15, 15, 15, 180 });
                draw_list.add_rect(container_x, y - 20.0f, container_width, (defuse_y + bar_height + 5.0f) - (y - 20.0f), zdraw::rgba{ 80, 80, 80, 255 }, 1.0f);

                float defuse_progress = std::clamp(defuse_left / defuse_length, 0.0f, 1.0f);
                draw_list.add_rect_filled(x, defuse_y, bar_width * (1.0f - defuse_progress), bar_height, zdraw::rgba{ 60, 180, 255, 255 });
                draw_list.add_rect(x, defuse_y, bar_width, bar_height, zdraw::rgba{ 40, 40, 40, 255 }, 1.0f);

                bool can_defuse = defuse_left < time_left;
                std::string status = can_defuse ? "DEFUSE DETECTED (SAFE)" : "BOMB WILL EXPLODE (FATAL)";
                zdraw::rgba status_color = can_defuse ? zdraw::rgba{ 60, 220, 100, 255 } : zdraw::rgba{ 255, 60, 60, 255 };
                
                float stw = 0.0f, sth = 0.0f;
                if (fnt)
                {
                    fnt->calc_text_size(status, stw, sth);
                }
                draw_list.add_text((screen_w - stw) / 2.0f, defuse_y + bar_height + 2.0f, status, nullptr, status_color, zdraw::text_style::outlined);
            }
        }
    }

    void visual_extras::draw_hitmarker(zdraw::draw_list& draw_list)
    {
        std::chrono::steady_clock::time_point last_hit;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            last_hit = m_last_hit_time;
        }

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - last_hit).count();
        if (elapsed < 0.25f)
        {
            float alpha = std::clamp(1.0f - (elapsed / 0.25f), 0.0f, 1.0f);
            
            float screen_w = ::GetSystemMetrics(SM_CXSCREEN);
            float screen_h = ::GetSystemMetrics(SM_CYSCREEN);
            float cx = screen_w / 2.0f;
            float cy = screen_h / 2.0f;

            zdraw::rgba color{ 255, 255, 255, static_cast<std::uint8_t>(alpha * 255.0f) };
            float size = 6.0f;
            float gap = 3.0f;

            draw_list.add_line(cx - size, cy - size, cx - gap, cy - gap, color, 1.5f);
            draw_list.add_line(cx + gap, cy - gap, cx + size, cy - size, color, 1.5f);
            draw_list.add_line(cx - size, cy + size, cx - gap, cy + gap, color, 1.5f);
            draw_list.add_line(cx + gap, cy + gap, cx + size, cy + size, color, 1.5f);
        }
    }

    void visual_extras::draw_damage_indicators(zdraw::draw_list& draw_list)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto now = std::chrono::steady_clock::now();
        
        // Remove old indicators
        std::erase_if(m_damage_indicators, [&](const damage_indicator_entry& entry) {
            return std::chrono::duration<float>(now - entry.spawn_time).count() >= 1.0f;
        });

        for (const auto& entry : m_damage_indicators)
        {
            float elapsed = std::chrono::duration<float>(now - entry.spawn_time).count();
            float alpha = std::clamp(1.0f - elapsed, 0.0f, 1.0f);

            math::vector2 screen_pos = systems::g_view.project(entry.world_pos);
            if (systems::g_view.projection_valid(screen_pos))
            {
                float float_y = elapsed * 40.0f;
                std::string dmg_text = "-" + std::to_string(entry.damage);
                
                zdraw::rgba text_color{ 255, 140, 40, static_cast<std::uint8_t>(alpha * 255.0f) };
                
                float tw = 0.0f, th = 0.0f;
                const auto fnt = zdraw::get_font();
                if (fnt)
                {
                    fnt->calc_text_size(dmg_text, tw, th);
                }

                draw_list.add_text(screen_pos.x - tw / 2.0f, screen_pos.y - float_y, dmg_text, nullptr, text_color, zdraw::text_style::outlined);
            }
        }
    }

} // namespace features::misc