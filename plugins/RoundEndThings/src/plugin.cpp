// RoundEndThings — Plugify port of the Metamod plugin RoundEndThings (ABKAM)
// (ref/src/RoundEndThings/RoundEndThings.cpp).
//
// Behaviour kept identical:
//   * settings.ini (bhop_enabled / speed_enabled / gravity_enabled / speed / gravity) of
//     addons/configs/RoundEndThings is read at startup (and again once the server is up, like the
//     original's StartupServer);
//   * on round_end: with bhop_enabled the server runs "sv_autobunnyhopping 1" and
//     "sv_enablebunnyhopping 1", and every alive player gets m_flVelocityModifier = speed and
//     m_flGravityScale = m_flActualGravityScale = gravity - unless the player opted out of that
//     boost in the !ret / mm_ret menu;
//   * on round_start: the bhop convars go back to 0 and the three per-pawn fields are restored
//     to 1.0;
//   * the menu ("Настройки конца раунда") shows one toggle item per enabled feature
//     ("Отключить скорость [X]" / "[ ]"), toggling prints "Настройка <скорости|гравитации>
//     изменена." and re-opens the menu, exactly like the original's callback;
//   * the two per-player choices persist in the cookies "roundend_disable_speed" and
//     "roundend_disable_gravity" (the names of the Metamod original).
//
// Port notes:
//   * ICookiesApi of the Pisex cookies plugin -> the official permissions cookies
//     (SetCookie(pluginId, steamid64, name, any, false) / GetCookie), driven like hudmenu does:
//     the values are written as int64 0/1 and read back accepting every numeric shape (the
//     Metamod-era store held the strings "0"/"1");
//   * HookClientCookieLoaded -> the permissions OnUserLoaded listener plus a once-per-second
//     reconcile, because the live server does not deliver every authorization event; a save that
//     arrived before the permissions user existed is retried until it succeeds;
//   * m_flVelocityModifier / m_flGravityScale / m_flActualGravityScale are schema writes on the
//     player pawn (CCSPlayerPawn / CBaseEntity) without a network state change - the original
//     wrote them through hl2sdk without SetStateChanged;
//   * engine->ServerCommand() -> s2sdk::ServerCommand() (it appends the newline itself);
//   * CCSPlayerController::FromSlot() + pawn/alive checks -> the pisexcompat player wrappers.

#include "roundend_core.hpp"

#include <permissions/permissions.hpp>

#include <pisex/pisex.hpp>

#include <plg/plugin.hpp>
#include <plg/source_location.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>

namespace {

constexpr const char* kLogTag = "[RoundEndThings]";
constexpr int kMaxSlots = 64;

constexpr const char* kConfigPath = "addons/configs/RoundEndThings/settings.ini";
constexpr const char* kCookieSpeed = "roundend_disable_speed";
constexpr const char* kCookieGravity = "roundend_disable_gravity";
constexpr const char* kConsoleCommand = "mm_ret";
constexpr const char* kChatCommand = "!ret";

constexpr auto kReconcileInterval = std::chrono::seconds(1);

// The pawn fields of the original's OnRoundEnd/OnRoundStart.
constexpr const char* kPawnClass = "CCSPlayerPawn";
constexpr const char* kVelocityField = "m_flVelocityModifier";
constexpr const char* kBaseEntityClass = "CBaseEntity";
constexpr const char* kGravityField = "m_flGravityScale";
constexpr const char* kActualGravityField = "m_flActualGravityScale";

void LogMessage(std::string_view message, plg::Severity severity) {
	if (plg::Log) {
		plg::Log(message, severity, plg::source_location::current());
	}
}

class RoundEndThingsPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// round_end / round_start of the original.
	void OnRoundEnd();
	void OnRoundStart();
	// The permissions OnUserLoaded body: refresh/save the settings of the slot owning the steamid.
	void OnUserLoaded(uint64_t targetId);
	// The !ret / mm_ret command (OpenRoundEndSettingsMenu of the original).
	void OpenSettingsMenu(int iSlot);
	// The menu callback bodies.
	roundend::PlayerSettings& PlayerSettingsOf(int iSlot);
	void MarkPendingSave(int iSlot);
	void LoadConfig();

private:
	void LoadSettingsMenu(int iSlot);
	void RefreshSlot(int iSlot, uint64_t steamid);
	void TryLoadSettings(int iSlot);
	void TrySaveSettings(int iSlot);
	void ReconcileSlots();

	struct SlotState {
		uint64_t steamid = 0;
		bool loaded = false;
		bool pendingSave = false;
	};

	roundend::Settings m_settings;
	std::array<roundend::PlayerSettings, kMaxSlots> m_players{};
	std::array<SlotState, kMaxSlots> m_slots{};

	bool m_permissionsAvailable = false;
	bool m_permissionsListenerRegistered = false;
	std::chrono::milliseconds m_sinceReconcile{0};
};

RoundEndThingsPlugin g_roundEndThingsPlugin;

// --- configuration ------------------------------------------------------------------------------

void RoundEndThingsPlugin::LoadConfig() {
	pisex::KeyValues kv("Config");
	if (!kv.LoadFromFile(kConfigPath)) {
		LogMessage(std::string(kLogTag) + " failed to load " + kConfigPath + " - keeping the built-in defaults",
				   plg::Severity::Error);
		return;
	}
	m_settings.bhopEnabled = kv.GetBool("bhop_enabled", true);
	m_settings.speedEnabled = kv.GetBool("speed_enabled", true);
	m_settings.gravityEnabled = kv.GetBool("gravity_enabled", true);
	m_settings.speed = kv.GetFloat("speed", 1.5f);
	m_settings.gravity = kv.GetFloat("gravity", 1.5f);
}

roundend::PlayerSettings& RoundEndThingsPlugin::PlayerSettingsOf(int iSlot) {
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return m_players[0];
	}
	return m_players[static_cast<std::size_t>(iSlot)];
}

void RoundEndThingsPlugin::MarkPendingSave(int iSlot) {
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	m_slots[static_cast<std::size_t>(iSlot)].pendingSave = true;
	TrySaveSettings(iSlot); // immediate write; the reconcile retries while it fails
}

// --- cookies -------------------------------------------------------------------------------------

void RoundEndThingsPlugin::TryLoadSettings(int iSlot) {
	if (!m_permissionsAvailable || !__permissions_GetCookie || iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	SlotState& state = m_slots[static_cast<std::size_t>(iSlot)];
	if (state.steamid == 0 || state.loaded) {
		return;
	}
	try {
		bool disable = false;
		plg::any value;
		const permissions::Status speedStatus = permissions::GetCookie(state.steamid, plg::string(kCookieSpeed), value);
		if (speedStatus == permissions::Status::Success) {
			if (roundend::CookieValueToBool(value, disable)) {
				m_players[static_cast<std::size_t>(iSlot)].disableSpeed = disable;
			} else {
				LogMessage(std::string(kLogTag) + " user " + std::to_string(state.steamid) + " has an unusable \"" +
						   kCookieSpeed + "\" cookie - keeping the default",
						   plg::Severity::Warning);
			}
		} else if (speedStatus != permissions::Status::CookieNotFound) {
			return; // TargetUserNotFound / DBNotReady: retried by the reconcile
		}

		const permissions::Status gravityStatus =
				permissions::GetCookie(state.steamid, plg::string(kCookieGravity), value);
		if (gravityStatus == permissions::Status::Success) {
			if (roundend::CookieValueToBool(value, disable)) {
				m_players[static_cast<std::size_t>(iSlot)].disableGravity = disable;
			} else {
				LogMessage(std::string(kLogTag) + " user " + std::to_string(state.steamid) + " has an unusable \"" +
						   kCookieGravity + "\" cookie - keeping the default",
						   plg::Severity::Warning);
			}
		} else if (gravityStatus != permissions::Status::CookieNotFound) {
			return;
		}

		// Both cookies are known (present or absent): the slot is loaded.
		state.loaded = true;
	} catch (const std::exception& ex) {
		LogMessage(std::string(kLogTag) + " reading the round-end cookies failed: " + ex.what(), plg::Severity::Error);
	} catch (...) {
		LogMessage(std::string(kLogTag) + " reading the round-end cookies failed with an unknown exception",
				   plg::Severity::Error);
	}
}

void RoundEndThingsPlugin::TrySaveSettings(int iSlot) {
	if (!m_permissionsAvailable || !__permissions_SetCookie || iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	SlotState& state = m_slots[static_cast<std::size_t>(iSlot)];
	if (state.steamid == 0 || !state.pendingSave) {
		return;
	}
	try {
		const roundend::PlayerSettings& settings = m_players[static_cast<std::size_t>(iSlot)];
		const permissions::Status speedStatus =
				permissions::SetCookie(static_cast<int64_t>(GetId()), state.steamid, plg::string(kCookieSpeed),
									   plg::any(static_cast<int64_t>(settings.disableSpeed ? 1 : 0)), false);
		if (speedStatus != permissions::Status::Success) {
			return; // TargetUserNotFound (user not loaded yet) / DBNotReady: retried later
		}
		const permissions::Status gravityStatus =
				permissions::SetCookie(static_cast<int64_t>(GetId()), state.steamid, plg::string(kCookieGravity),
									   plg::any(static_cast<int64_t>(settings.disableGravity ? 1 : 0)), false);
		if (gravityStatus != permissions::Status::Success) {
			return;
		}
		state.pendingSave = false;
		state.loaded = true;
	} catch (const std::exception& ex) {
		LogMessage(std::string(kLogTag) + " saving the round-end cookies failed: " + ex.what(), plg::Severity::Error);
	} catch (...) {
		LogMessage(std::string(kLogTag) + " saving the round-end cookies failed with an unknown exception",
				   plg::Severity::Error);
	}
}

void RoundEndThingsPlugin::RefreshSlot(int iSlot, uint64_t steamid) {
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	SlotState& state = m_slots[static_cast<std::size_t>(iSlot)];
	if (state.steamid != steamid) {
		// The slot changed hands: the new player starts from the defaults until the cookies load.
		state = SlotState{};
		state.steamid = steamid;
		m_players[static_cast<std::size_t>(iSlot)] = roundend::PlayerSettings{};
	}
	TryLoadSettings(iSlot);
}

void RoundEndThingsPlugin::OnUserLoaded(uint64_t targetId) {
	if (targetId == 0) {
		return;
	}
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		if (m_slots[static_cast<std::size_t>(slot)].steamid == targetId) {
			TryLoadSettings(slot);
			TrySaveSettings(slot);
		}
	}
}

void RoundEndThingsPlugin::ReconcileSlots() {
	if (!pisex::IsGameReady() || !m_permissionsAvailable) {
		return;
	}
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		try {
			if (!pisex::g_pPlayers->IsConnected(slot) || !pisex::g_pPlayers->IsAuthenticated(slot) ||
				pisex::g_pPlayers->IsFakeClient(slot)) {
				continue;
			}
			const uint64_t steamid = pisex::g_pPlayers->GetSteamID64(slot);
			if (steamid == 0) {
				continue;
			}
			RefreshSlot(slot, steamid);
			TrySaveSettings(slot);
		} catch (const std::exception& ex) {
			LogMessage(std::string(kLogTag) + " reconcile of slot " + std::to_string(slot) + " failed: " + ex.what(),
					   plg::Severity::Error);
		} catch (...) {
			LogMessage(std::string(kLogTag) + " reconcile of slot " + std::to_string(slot) + " failed",
					   plg::Severity::Error);
		}
	}
}

// --- the round-end behaviour ----------------------------------------------------------------------

namespace {

// One schema float write on the player's pawn (no network state change: the original wrote these
// movement/physics fields server-side only, without SetStateChanged).
void SetPawnFloat(int iSlot, const char* className, const char* field, float value) {
	const int pawn = pisex::GetPlayerPawn(iSlot);
	if (pawn == 0) {
		return;
	}
	pisex::SetEntFloat(pawn, className, field, value, false);
}

void ServerCommand(const char* command) {
	if (!__s2sdk_ServerCommand) {
		return;
	}
	try {
		s2sdk::ServerCommand(plg::string(command));
	} catch (...) {
	}
}

} // namespace

void RoundEndThingsPlugin::OnRoundEnd() {
	if (m_settings.bhopEnabled) {
		ServerCommand("sv_autobunnyhopping 1");
		ServerCommand("sv_enablebunnyhopping 1");
	}

	for (int iSlot = 0; iSlot < kMaxSlots; ++iSlot) {
		try {
			if (!pisex::g_pPlayers->IsAlive(iSlot)) {
				continue;
			}
			const roundend::PlayerSettings& player = m_players[static_cast<std::size_t>(iSlot)];

			if (m_settings.speedEnabled && !player.disableSpeed) {
				SetPawnFloat(iSlot, kPawnClass, kVelocityField, m_settings.speed);
			} else {
				SetPawnFloat(iSlot, kPawnClass, kVelocityField, roundend::kDefaultSpeed);
			}

			if (m_settings.gravityEnabled && !player.disableGravity) {
				SetPawnFloat(iSlot, kBaseEntityClass, kGravityField, m_settings.gravity);
				SetPawnFloat(iSlot, kBaseEntityClass, kActualGravityField, m_settings.gravity);
			} else {
				SetPawnFloat(iSlot, kBaseEntityClass, kGravityField, roundend::kDefaultGravity);
				SetPawnFloat(iSlot, kBaseEntityClass, kActualGravityField, roundend::kDefaultGravity);
			}
		} catch (const std::exception& ex) {
			LogMessage(std::string(kLogTag) + " round-end boost of slot " + std::to_string(iSlot) + " failed: " +
					   ex.what(), plg::Severity::Error);
		} catch (...) {
			LogMessage(std::string(kLogTag) + " round-end boost of slot " + std::to_string(iSlot) + " failed",
					   plg::Severity::Error);
		}
	}
}

void RoundEndThingsPlugin::OnRoundStart() {
	if (m_settings.bhopEnabled) {
		ServerCommand("sv_autobunnyhopping 0");
		ServerCommand("sv_enablebunnyhopping 0");
	}

	for (int iSlot = 0; iSlot < kMaxSlots; ++iSlot) {
		try {
			if (!pisex::g_pPlayers->IsAlive(iSlot)) {
				continue;
			}
			SetPawnFloat(iSlot, kPawnClass, kVelocityField, roundend::kDefaultSpeed);
			SetPawnFloat(iSlot, kBaseEntityClass, kGravityField, roundend::kDefaultGravity);
			SetPawnFloat(iSlot, kBaseEntityClass, kActualGravityField, roundend::kDefaultGravity);
		} catch (const std::exception& ex) {
			LogMessage(std::string(kLogTag) + " round-start restore of slot " + std::to_string(iSlot) + " failed: " +
					   ex.what(), plg::Severity::Error);
		} catch (...) {
			LogMessage(std::string(kLogTag) + " round-start restore of slot " + std::to_string(iSlot) + " failed",
					   plg::Severity::Error);
		}
	}
}

// --- the menu ------------------------------------------------------------------------------------

void RoundEndThingsPlugin::LoadSettingsMenu(int iSlot) {
	pisex::Menu menu;
	pisex::SetTitleMenu(menu, "Настройки конца раунда");
	if (m_settings.speedEnabled) {
		pisex::AddItemMenu(menu, "speed", roundend::SpeedMenuLabel(PlayerSettingsOf(iSlot)).c_str());
	}
	if (m_settings.gravityEnabled) {
		pisex::AddItemMenu(menu, "gravity", roundend::GravityMenuLabel(PlayerSettingsOf(iSlot)).c_str());
	}
	pisex::SetExitMenu(menu, true);
	pisex::SetCallback(menu, [](const char* szBack, const char*, int iItem, int iSlot) {
		// The original's callback: anything but the exit/exit-back actions with a known key.
		if (iItem < 7 && szBack != nullptr) {
			roundend::PlayerSettings& settings = g_roundEndThingsPlugin.PlayerSettingsOf(iSlot);
			if (std::strcmp(szBack, "speed") == 0) {
				settings.disableSpeed = !settings.disableSpeed;
				pisex::g_pUtils->PrintToChat(iSlot, "Настройка скорости изменена.");
			} else if (std::strcmp(szBack, "gravity") == 0) {
				settings.disableGravity = !settings.disableGravity;
				pisex::g_pUtils->PrintToChat(iSlot, "Настройка гравитации изменена.");
			}
			g_roundEndThingsPlugin.MarkPendingSave(iSlot);
			g_roundEndThingsPlugin.OpenSettingsMenu(iSlot);
		}
	});
	pisex::DisplayPlayerMenu(menu, iSlot);
}

void RoundEndThingsPlugin::OpenSettingsMenu(int iSlot) {
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return; // the console has no menu to open (the original passed the slot through)
	}
	LoadSettingsMenu(iSlot);
}

// --- lifecycle ------------------------------------------------------------------------------------

void OnPermissionsUserLoadedTrampoline(int64_t pluginID, uint64_t targetId, permissions::PlayerState state) {
	(void)pluginID;
	(void)state;
	try {
		g_roundEndThingsPlugin.OnUserLoaded(targetId);
	} catch (const std::exception& ex) {
		LogMessage(std::string(kLogTag) + " exception in OnUserLoaded: " + ex.what(), plg::Severity::Error);
	} catch (...) {
		LogMessage(std::string(kLogTag) + " unknown exception in OnUserLoaded", plg::Severity::Error);
	}
}

plg::PluginResult RoundEndThingsPlugin::OnPluginStart() {
	try {
		pisex::Init("RoundEndThings");
		LoadConfig();

		m_permissionsAvailable = __permissions_GetCookie && __permissions_SetCookie &&
								 __permissions_OnUserLoaded_Register && __permissions_OnUserLoaded_Unregister;
		if (!m_permissionsAvailable) {
			LogMessage(std::string(kLogTag) + " permissions bindings are not available - the per-player "
											  "round-end settings cannot be loaded or saved",
					   plg::Severity::Warning);
		}

		pisex::g_pUtils->RegCommand({kConsoleCommand}, {kChatCommand}, [](int iSlot, const char*) {
			g_roundEndThingsPlugin.OpenSettingsMenu(iSlot);
			return true;
		});

		pisex::g_pUtils->HookEvent("round_end", [](const char*, pisex::IGameEvent*, bool) {
			g_roundEndThingsPlugin.OnRoundEnd();
		});
		pisex::g_pUtils->HookEvent("round_start", [](const char*, pisex::IGameEvent*, bool) {
			g_roundEndThingsPlugin.OnRoundStart();
		});

		// The original re-read its config once the server was up.
		pisex::g_pUtils->StartupServer([]() {
			g_roundEndThingsPlugin.LoadConfig();
		});

		if (m_permissionsAvailable) {
			try {
				const permissions::Status status = permissions::OnUserLoaded_Register(&OnPermissionsUserLoadedTrampoline);
				if (status == permissions::Status::Success) {
					m_permissionsListenerRegistered = true;
				} else {
					LogMessage(std::string(kLogTag) + " registering the permissions OnUserLoaded listener failed "
													  "(status " + std::to_string(static_cast<int32_t>(status)) +
													  ") - settings load from the reconcile instead",
							   plg::Severity::Warning);
				}
			} catch (...) {
				LogMessage(std::string(kLogTag) + " registering the permissions OnUserLoaded listener failed",
						   plg::Severity::Warning);
			}
		}
	} catch (const std::exception& ex) {
		LogMessage(std::string(kLogTag) + " OnPluginStart failed: " + ex.what(), plg::Severity::Error);
	} catch (...) {
		LogMessage(std::string(kLogTag) + " OnPluginStart failed with an unknown exception", plg::Severity::Error);
	}
	return {}; // never Failed: that stops every plugify plugin
}

plg::PluginResult RoundEndThingsPlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	try {
		pisex::Poll();
		m_sinceReconcile += dt;
		if (m_sinceReconcile >= kReconcileInterval) {
			m_sinceReconcile = std::chrono::milliseconds(0);
			ReconcileSlots();
		}
	} catch (const std::exception& ex) {
		LogMessage(std::string(kLogTag) + " update failed: " + ex.what(), plg::Severity::Error);
	} catch (...) {
		LogMessage(std::string(kLogTag) + " update failed with an unknown exception", plg::Severity::Error);
	}
	return {};
}

plg::PluginResult RoundEndThingsPlugin::OnPluginEnd() {
	try {
		if (m_permissionsListenerRegistered) {
			m_permissionsListenerRegistered = false;
			if (__permissions_OnUserLoaded_Unregister) {
				try {
					permissions::OnUserLoaded_Unregister(&OnPermissionsUserLoadedTrampoline);
				} catch (...) {
				}
			}
		}
		pisex::Shutdown();
	} catch (const std::exception& ex) {
		LogMessage(std::string(kLogTag) + " OnPluginEnd failed: " + ex.what(), plg::Severity::Error);
	} catch (...) {
		LogMessage(std::string(kLogTag) + " OnPluginEnd failed with an unknown exception", plg::Severity::Error);
	}
	return {};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_roundEndThingsPlugin)
