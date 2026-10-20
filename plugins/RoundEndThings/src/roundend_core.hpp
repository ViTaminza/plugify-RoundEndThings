// RoundEndThings — engine-independent core: the per-player settings of the original and the menu
// labels / cookie values they are stored with. The cookie value readers accept every numeric shape
// the permissions store may hold (this plugin writes int64 0/1; the Metamod-era Pisex cookie store
// held the strings "0"/"1").

#pragma once

#include <plg/any.hpp>

#include <string>

namespace roundend {

// The [Config] settings.ini block (types as in the original's globals).
struct Settings {
	bool bhopEnabled = true;	// bhop_enabled
	bool speedEnabled = true;	// speed_enabled
	bool gravityEnabled = true; // gravity_enabled
	float speed = 1.5f;			// speed (m_flVelocityModifier applied at round end)
	float gravity = 1.5f;		// gravity (m_flGravityScale/m_flActualGravityScale)
};

// g_PlayerSettings[iSlot] of the original: "the player opted out of the round-end boost".
struct PlayerSettings {
	bool disableSpeed = false;
	bool disableGravity = false;
};

// The two menu item labels ("Отключить скорость [X]" / "[ ]"), built exactly like the original's
// ternary expressions.
std::string SpeedMenuLabel(const PlayerSettings& settings);
std::string GravityMenuLabel(const PlayerSettings& settings);

// Cookie values ("roundend_disable_speed" / "roundend_disable_gravity"): true = "0"/0/false, the
// original's atoi() != 0 semantics. Returns false when the value is none of the numeric shapes.
bool CookieValueToBool(const plg::any& value, bool& out);

// The DEFAULT_SPEED / DEFAULT_GRAVITY restoration value of the original.
inline constexpr float kDefaultSpeed = 1.0f;
inline constexpr float kDefaultGravity = 1.0f;

} // namespace roundend
