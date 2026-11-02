// RoundEndThings — engine-independent core: the settings of the original's settings.ini, the
// per-player opt-outs, the original's menu text and the cookie value readers.
//
// Everything here is free of s2sdk, of the Plugify host and of the engine, so the tests can drive
// it directly (tests/roundend_core_tests.cpp). src/plugin.cpp owns the s2sdk side and only asks
// this core what the values/menus look like.
//
// The original RoundEndThings has no phrase file: its menu title, item labels and the two chat
// replies are literals in RoundEndThings.cpp. They are kept byte-for-byte here (kMenuTitle,
// SpeedMenuLabel(), GravityMenuLabel(), kSpeedChanged, kGravityChanged); there is no
// ServerLang/core.cfg phrase chain to port because the original never localized them, and the
// production addons tree has no RoundEndThings.phrases.txt either.

#pragma once

#include "kv1.hpp"

#include <plg/any.hpp>

#include <string>
#include <string_view>
#include <vector>

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

// The DEFAULT_SPEED / DEFAULT_GRAVITY restoration value of the original.
inline constexpr float kDefaultSpeed = 1.0f;
inline constexpr float kDefaultGravity = 1.0f;

// The original's hardcoded menu (OpenRoundEndSettingsMenu) and chat replies.
inline constexpr const char* kMenuTitle = "Настройки конца раунда";
inline constexpr const char* kSpeedInfo = "speed";
inline constexpr const char* kGravityInfo = "gravity";
inline constexpr const char* kSpeedChanged = "Настройка скорости изменена.";
inline constexpr const char* kGravityChanged = "Настройка гравитации изменена.";

// The two menu item labels ("Отключить скорость [X]" / "[ ]"), built exactly like the original's
// ternary expressions.
std::string SpeedMenuLabel(const PlayerSettings& settings);
std::string GravityMenuLabel(const PlayerSettings& settings);

// Cookie values ("roundend_disable_speed" / "roundend_disable_gravity"): every numeric shape the
// permissions store may hold (this plugin writes int64 0/1; the Metamod-era Pisex cookie store
// held the strings "0"/"1"). Returns false when the value is none of the numeric shapes, so the
// caller keeps the default.
bool CookieValueToBool(const plg::any& value, bool& out);

// The item the player selected, resolved from the menu item's internal info string (the original
// compared szBack with "speed"/"gravity").
enum class MenuChoice {
	None,
	Speed,
	Gravity,
};

MenuChoice ChoiceFromInfo(std::string_view info);

// Applies the original's callback body for a known choice: flips the flag and returns the exact
// chat reply ("Настройка скорости изменена." / "Настройка гравитации изменена."), or nullptr for
// MenuChoice::None (no flag is flipped then).
const char* ApplyChoice(PlayerSettings& settings, MenuChoice choice);

// Parses the "Config" { ... } block with the types/defaults of the original's LoadConfig().
Settings ParseSettings(const KvNode& root);

// The settings.ini candidates in the order pisexcompat's ResolvePath tried them for the unchanged
// original path "addons/configs/RoundEndThings/settings.ini":
//   1. the raw cwd-relative Metamod path,
//   2. <configs>/RoundEndThings/settings.ini,
//   3. <configs>/RoundEndThings/translations/settings.ini,
//   4. <data>/RoundEndThings/settings.ini.
// An empty root keeps pisexcompat's JoinPath("", name) spelling (the relative "RoundEndThings/...").
std::vector<std::string> ConfigCandidates(std::string_view configsRoot, std::string_view dataRoot);

// The file name the original passed to KeyValues::LoadFromFile.
inline constexpr const char* kConfigFileName = "settings.ini";
// The plugin folder of the manifest / of <configs>/.
inline constexpr const char* kPluginDir = "RoundEndThings";
// The exact original path pisexcompat made relative to the server working directory first.
inline constexpr const char* kConfigRawPath = "addons/configs/RoundEndThings/settings.ini";

// The outcome of LoadSettings(): the first existing candidate is authoritative, exactly like
// pisexcompat's FirstExisting() + KeyValues::LoadFromFile() pair. `found` = a candidate exists,
// `parsed` = it parsed, `settings` = the values (the built-in defaults when parsing failed).
struct ConfigLoadResult {
	bool found = false;
	bool parsed = false;
	Settings settings;
	// The candidate that was read (or the most likely one when nothing exists, for the log line).
	std::string path;
};

ConfigLoadResult LoadSettings(const std::vector<std::string>& candidates);

// JoinPath() of pisexcompat: joins with one '/', "" for an empty side.
std::string JoinPath(std::string_view a, std::string_view b);

} // namespace roundend
