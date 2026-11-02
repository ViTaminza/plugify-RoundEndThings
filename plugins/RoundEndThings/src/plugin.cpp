// RoundEndThings — Plugify port of the Metamod plugin RoundEndThings (ABKAM), now on the native
// s2sdk API (docs/PLUGIN_STANDARD.md §7: no pisexcompat, no guard library, no own wrapper library).
// The optional hudmenu *API* dependency is kept on purpose: the player's own "!menu" type choice is
// real functionality the old pisexcompat menu layer honoured, and the generated hudmenu headers/
// bindings are an API surface, not a wrapper library.
//
// Behaviour kept identical:
//   * settings.ini (bhop_enabled / speed_enabled / gravity_enabled / speed / gravity) of
//     addons/configs/RoundEndThings is read at startup and again once the server is up (the
//     original's StartupServer), with the original's KeyValues defaults;
//   * on round_end: with bhop_enabled the server runs "sv_autobunnyhopping 1" and
//     "sv_enablebunnyhopping 1", and every alive player's pawn gets m_flVelocityModifier = speed and
//     m_flGravityScale = m_flActualGravityScale = gravity - unless the player opted out of that
//     boost in the !ret / mm_ret menu;
//   * on round_start: the bhop convars go back to 0 and the three per-pawn fields are restored to
//     1.0 for every alive player;
//   * the menu ("Настройки конца раунда") shows one toggle item per enabled feature
//     ("Отключить скорость [X]" / "[ ]"), toggling prints "Настройка <скорости|гравитации>
//     изменена." and re-opens the menu, exactly like the original's callback;
//   * the two per-player choices persist in the cookies "roundend_disable_speed" and
//     "roundend_disable_gravity" (the names of the Metamod original), through the official
//     permissions cookies, keyed by the player's SteamID64 like the HEAD port did;
//   * "mm_ret" (console) and "!ret" (chat) open the menu.
//
// Pure s2sdk: every safety rule the old pisexcompat/review checklist asks for lives in this file.
//
//   * A1: OnPluginStart never returns Failed (that stops every Plugify plugin); every failure is
//     logged and only its feature is disabled.
//   * A7/C12: OnPluginEnd takes back every registration - the two events (UnhookEvent), the map
//     listeners, both commands (RemoveCommand for the ones this plugin created, RemoveCommandListener
//     for a name that already existed), the permissions OnUserLoaded listener, every menu this
//     plugin created and the reserved id-0 dummy - so `plg reload` leaves nothing that points into
//     the unloaded library. There is no s2sdk timer to kill (see the cadence note below).
//     Every registration requires its complete teardown pair *before* it happens (and the command
//     ownership is only known after AddConsoleCommand, so both command removers are required):
//     commands AddConsoleCommand+RemoveCommand+RemoveCommandListener, events HookEvent+UnhookEvent,
//     map listeners OnMapStart/OnMapEnd Register+Unregister, permissions GetCookie+SetCookie+
//     OnUserLoaded_Register+OnUserLoaded_Unregister. A missing half disables that feature with one
//     log line instead of leaving a dangling callback.
//   * B1/B2: the map gate (src/map_gate.hpp). No client, entity or schema call happens before the
//     first map and in the window between OnMapEnd and the next OnMapStart. Readiness is granted by
//     OnMapStart, or by the first OnPluginUpdate for a plugin loaded into a running map (only when
//     no OnMapEnd was seen since OnPluginStart); OnMapEnd only clears state. OnMapEnd never touches
//     a client or an entity.
//   * C2: HasController() validates the controller pointer (PlayerSlotToEntPointer +
//     IsValidEntPointer), its CEntityHandle (PlayerSlotToEntHandle, neither 0 nor -1, and the
//     engine's IsValidEntHandle) and its entity index (> 0); IsInGameSlot() is the standard
//     connected + in-game + not-SourceTV check. PITFALLS C1: a valid CEntityHandle can be negative
//     (serial into bit 31), so its sign is never used to reject it (src/ent_handle.hpp).
//   * C6/C8: the three pawn fields are written with RAW offsets: GetSchemaOffset() is called only
//     while the gate is open, only successful offsets (>= 0) are cached (and a failed lookup is
//     retried on the next round event instead of poisoning the cache), and the write is
//     s2sdk::SetEntDataFloat(handle, offset, value, 4, changeState=false, chainOffset=-2) - the
//     original assigned the fields through hl2sdk without SetStateChanged, and the HEAD port
//     documented the same. No typed SetEntSchema* call is made, so neither the map-start "schema
//     type poison" (C6) nor the CS2BadList silent no-op (C7) can hit these writes.
//   * C11: "mm_ret" is this plugin's own console command and "ret" its chat spelling (s2sdk maps
//     chat "!ret" to the registered name "ret" and passes ConCommandContext::Chat). Each handler
//     answers only its own context, so "!mm_ret" stays an ordinary say line and a console "ret"
//     behaves as if the plugin were not loaded. Ownership is tracked: AddConsoleCommand() returns
//     false when the name already existed but still attaches the listener, and OnPluginEnd then
//     removes only that listener (RemoveCommandListener) instead of somebody else's command.
//   * C12: both events are hooked with s2sdk::HookEvent(..., HookMode::Pre) - the point the old
//     pisexcompat HookEvent wrapped - and every callback answers Continue, so an event is never
//     swallowed for the engine or the other plugins. round_end/round_start carry no player-typed
//     fields, so neither GetEventPlayerSlot nor GetEventInt is needed; the callback bodies read no
//     event field at all.
//
// Cadence (why no s2sdk timer): s2sdk's TimerSystem::RunFrame() holds a non-recursive mutex while
// it invokes a timer callback (ref/s2sdk-src/src/core/timer_system.cpp), and a timer that re-arms
// itself from inside the callback (or a callback that reaches another plugin creating/killing a
// timer) locks the same mutex on the same thread and deadlocks the server. The once-per-second
// permission reconcile is therefore an accumulator in OnPluginUpdate: a plain host callback, no
// timer lock held, at most one reconcile per update, no unbounded catch-up, and the accumulator is
// dropped whenever the gate closes. A delay of 0 never exists (PITFALLS C9).
//
// Menu lifetime (PITFALLS C10): s2sdk never destroys a menu by itself and its handler runs under
// the menu manager lock. The handler therefore records only the selection (the item index mapped to
// the original's "speed"/"gravity" choice, plus the owner identity captured when the menu opened);
// the settings change, the chat reply, the cookie write and the re-open happen from
// OnPluginUpdate, after the slot's SteamID64 *and* controller handle/serial were re-checked - a
// reused slot never gets the previous player's change or message. DestroyMenu always runs outside
// the handler (OnPluginUpdate / OnPluginEnd), also for a menu whose display already ended.
//
// MenuId 0: CreateMenu can return 0, but s2sdk's menu manager uses 0 as the "no menu" value of its
// client state (menu_manager.cpp:507/538/770), so a menu with id 0 can be displayed but never
// cancelled, selected or destroyed through the client state. The plugin burns id 0 at start exactly
// like pisexcompat's BurnMenuIdZero (the dummy is destroyed in OnPluginEnd), so every menu it creates
// has an id >= 1 and the sentinel can never be confused with one of ours.
//
// Menu teardown between maps: from OnMapEnd nothing may touch a client/entity. Every destruction
// goes through DestroyMenuChecked(), the fail-safe graffiti established:
//   1. a still shown menu is ended with CancelClientMenu(slot, MenuCancelReason::Disconnect) -
//      s2sdk's own teardown for a lost client skips the backend's close callback (hudmenu's
//      HideForSlot, PrintHtmlCentre, ...); when Cancel answers false the display is re-checked with
//      GetClientMenu (false means "already ended", so DestroyMenu then makes no backend call);
//   2. if a display is somehow still there in the map-down window (an s2sdk MenuManager contract
//      violation), the menu is switched to the builtin "chat"/"console" type with SetMenuType
//      (verified with GetMenuType) before DestroyMenu - those closers are no-ops, so the destroy is
//      safe between maps AND no handler of this library survives OnPluginEnd;
//   3. an id that still cannot be released is reported as A7_UNRESOLVED, never silently treated as
//      cleaned up. The menu type API is therefore part of the required binding set: no menu
//      (including the reserved dummy) is created unless the fail-safe can work.
// With an open map (OnPluginEnd on a running server) the normal DestroyMenu path may clear a live
// viewer through the backend, which is legal there.
//
// Menu type: hudmenu's per-player answer (the "!menu" choice: "hud"/"wasd"/"button"/"centerhtml"/
// "chat") wins when it names a known backend, otherwise the original's default HUD_LAYOUT mapping of
// pisexcompat ResolveS2sdkMenuTypeName() is used - "hud" when the backend is registered, else
// s2sdk's on-screen WASD "button". No hudmenu plugin installed: the default path still works.
//
// Cookie identity: the slot -> SteamID64 mapping is recorded before the menu is built (a freshly
// joined slot used to have steamid 0 for up to the first reconcile, and a toggle in that window
// would have been overwritten by the reconcile's cookie load). A pending choice remembers which of
// the two cookies the player changed, so a later load merges the *other* stored value instead of
// overwriting the choice; a slot that changed hands still drops the pending save (the new occupant
// never asked for it). Every reconcile is one update-driven tick, never an s2sdk timer.
//
// Config (production file, no conversion): the exact pisexcompat candidate order is kept - the raw
// cwd "addons/configs/RoundEndThings/settings.ini" first, then <configs>/RoundEndThings,
// <configs>/RoundEndThings/translations and <data>/RoundEndThings; the first existing file is
// authoritative even when malformed (the caller keeps the built-in defaults and logs, it never
// silently loads a lower-priority copy). The file is read with the plugin-local KV1 reader
// (src/kv1.hpp), whose GetBool/GetFloat match the pisexcompat KeyValues the HEAD port used.
// The original has no phrase file (its menu/chat strings are literals), so there is no phrase path
// and no ServerLang chain here: the production addons tree has no RoundEndThings.phrases.txt, and
// inventing one would change the visible text.
//
// Fail soft everywhere: a missing binding, config file or permissions listener is logged and skips
// only the affected feature; no callback lets an exception escape into s2sdk or the engine.

#include "ent_handle.hpp"
#include "kv1.hpp"
#include "map_gate.hpp"
#include "roundend_core.hpp"

#include <hudmenu/hudmenu.hpp>
#include <permissions/permissions.hpp>

#include <plg/plugin.hpp>
#include <plg/source_location.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kLogTag = "[RoundEndThings]";

// s2sdk player slots are 0..63 (the engine's MAXPLAYERS); every client export indexes its client
// list with the slot, so the range is checked before the first call.
constexpr int kMaxSlots = 64;

// The two cookie names of the Metamod original.
constexpr const char* kCookieSpeed = "roundend_disable_speed";
constexpr const char* kCookieGravity = "roundend_disable_gravity";

// The original's RegCommand({"mm_ret"}, {"!ret"}): s2sdk maps the chat spelling "!ret" to the
// registered command name "ret" and marks the call ConCommandContext::Chat.
constexpr const char* kConsoleCommand = "mm_ret";
constexpr const char* kChatCommand = "ret";

// The original's one-second reconcile of the permissions cookies (the HEAD port's
// kReconcileInterval). Accumulated from OnPluginUpdate; no s2sdk timer exists.
constexpr double kReconcileIntervalSeconds = 1.0;

// The pawn fields of the original's OnRoundEnd/OnRoundStart.
constexpr const char* kPawnClass = "CCSPlayerPawn";
constexpr const char* kVelocityField = "m_flVelocityModifier";
constexpr const char* kBaseEntityClass = "CBaseEntity";
constexpr const char* kGravityField = "m_flGravityScale";
constexpr const char* kActualGravityField = "m_flActualGravityScale";

// The menu backends plugins/hudmenu registers. The original's default was Pisex's HUD_LAYOUT,
// which pisexcompat maps to "hud" when the backend is registered and to s2sdk's WASD "button"
// otherwise (libs/pisexcompat/src/menu.cpp ResolveS2sdkMenuTypeName).
constexpr const char* kHudMenuType = "hud";
constexpr const char* kWasdMenuType = "wasd";
constexpr const char* kButtonMenuType = "button";
constexpr const char* kChatMenuType = "chat";
constexpr const char* kConsoleMenuType = "console";
constexpr const char* kCenterHtmlMenuType = "centerhtml";

// Title of the reserved id-0 menu (see BurnReservedMenuId): it is never displayed.
constexpr const char* kReservedMenuTitle = "RoundEndThings:reserved-id-0";

// The flags pisexcompat's RegisterS2sdkName gave every RegCommand name (Release |
// ClientCanExecute): a client may execute both spellings.
template <typename... TFlags>
constexpr s2sdk::ConVarFlag CombineFlags(TFlags... flags) {
	return static_cast<s2sdk::ConVarFlag>((static_cast<int64_t>(flags) | ...));
}

constexpr s2sdk::ConVarFlag kCommandFlags =
		CombineFlags(s2sdk::ConVarFlag::Release, s2sdk::ConVarFlag::ClientCanExecute);

// --- logging -------------------------------------------------------------------------------------

void LogMessage(std::string_view message, plg::Severity severity) {
	if (plg::Log) {
		plg::Log(message, severity, plg::source_location::current());
	}
}

void LogInfo(std::string_view message) {
	LogMessage(message, plg::Severity::Info);
}

void LogWarn(std::string_view message) {
	LogMessage(message, plg::Severity::Warning);
}

void LogError(std::string_view message) {
	LogMessage(message, plg::Severity::Error);
}

std::string ToStd(const plg::string& text) {
	return std::string(text.data(), text.size());
}

// --- the plugin's state --------------------------------------------------------------------------

roundend::Settings g_settings;
std::array<roundend::PlayerSettings, kMaxSlots> g_players{};

// Which cookie of a pending save the player actually changed. The other one may still be unknown
// (the permissions user is not loaded yet), so a later TryLoadSettings() merges its stored value in
// instead of overwriting the player's choice.
constexpr uint8_t kPendingSpeed = 1;
constexpr uint8_t kPendingGravity = 2;

// The steamid last seen in a slot and the two cookie flags of the HEAD port's SlotState.
struct SlotState {
	uint64_t steamid = 0;
	bool loaded = false;
	bool pendingSave = false;
	uint8_t pendingFeatures = 0;
};

std::array<SlotState, kMaxSlots> g_slots{};

bool g_permissionsAvailable = false;
bool g_permissionsListenerRegistered = false;

// The reconcile cadence (see the header): accumulated from OnPluginUpdate, never an s2sdk timer.
double g_reconcileAccumulator = 0.0;

// PITFALLS B1/B2.
roundend::MapGate g_mapGate;

// The original's StartupServer re-read settings.ini once the server was up; this port does it on
// the first update with an open gate (the shipped semantics of the HEAD port's pisex::StartupServer
// callback).
bool g_startupConfigReloaded = false;

// PITFALLS C8: cached raw schema offsets; -1 = not resolved yet (or not available) and retried on
// the next round event.
int32_t g_velocityOffset = -1;
int32_t g_gravityOffset = -1;
int32_t g_actualGravityOffset = -1;
bool g_loggedSchemaFailure = false;

// --- menus (PITFALLS C10) ------------------------------------------------------------------------

// The menu this plugin currently owns for a slot.
//
// MenuId 0 is burned at start (BurnReservedMenuId), so every menu this plugin creates has an id
// >= 1 and s2sdk's "0 = no menu" client-state sentinel can never be confused with one of ours.
// std::optional still carries the local "nothing owned" state instead of the id value.
//
// The selection handler runs under the menu manager's lock, so it only records the intent
// (choicePending/pendingChoice); the toggle, the chat reply, the cookie write and the re-open are
// applied from OnPluginUpdate, after the slot's full owner identity (SteamID64 + controller
// handle/serial captured when the menu was opened) has been re-checked. A slot reused in that
// window can therefore never receive the previous player's change, message or cookie.
struct SlotMenu {
	std::optional<s2sdk::MenuId> handle;
	bool reopenPending = false;
	bool choicePending = false;
	roundend::MenuChoice pendingChoice = roundend::MenuChoice::None;
	uint64_t steamid = 0;
	int32_t controllerHandle = 0;
	// The menu's items, index-aligned with s2sdk's AddMenuItem order, resolved without a menu call
	// from the handler.
	std::array<roundend::MenuChoice, 2> itemChoices{};
	int itemCount = 0;
};

std::array<SlotMenu, kMaxSlots> g_menus{};

// One menu instance waiting for DestroyMenu outside the menu handler (PITFALLS C10). The slot is
// kept so the map-down teardown can end a still-displayed session with the Disconnect reason before
// destroying it. Processed from OnPluginUpdate and flushed by OnPluginEnd.
struct MenuDestroyRequest {
	int slot = -1;
	s2sdk::MenuId id = 0;
};

std::vector<MenuDestroyRequest> g_destroyQueue;

bool g_tearingDown = false;
bool g_loggedMenuUnavailable = false;
bool g_loggedMissingIdentity = false;
// This plugin created the reserved s2sdk menu id 0 and must destroy it before unloading, or its
// handler points into unloaded code (pisexcompat's BurnMenuIdZero/ReleaseReservedMenuId).
bool g_reservedMenuIdZero = false;

// --- registrations OnPluginEnd has to take back (PITFALLS A7) ------------------------------------

struct CommandRegistration {
	const char* name = nullptr;
	s2sdk::ConCommandCallback callback = nullptr;
	s2sdk::ConVarFlag flags = s2sdk::ConVarFlag::None;
	bool attempted = false;
	bool owned = false;
};

s2sdk::ResultType OnMmRetCommand(int32_t caller, s2sdk::ConCommandContext context,
								 const plg::vector<plg::string>& args);
s2sdk::ResultType OnRetCommand(int32_t caller, s2sdk::ConCommandContext context,
							   const plg::vector<plg::string>& args);

CommandRegistration g_commands[] = {
		{kConsoleCommand, &OnMmRetCommand, kCommandFlags, false, false},
		{kChatCommand, &OnRetCommand, kCommandFlags, false, false},
};

struct EventRegistration {
	const char* name = nullptr;
	s2sdk::EventCallback callback = nullptr;
	bool hooked = false;
};

s2sdk::ResultType OnRoundEndEvent(const plg::string& name, void* event, bool dontBroadcast);
s2sdk::ResultType OnRoundStartEvent(const plg::string& name, void* event, bool dontBroadcast);

EventRegistration g_events[] = {
		{"round_end", &OnRoundEndEvent, false},
		{"round_start", &OnRoundStartEvent, false},
};

// --- map gate helpers (PITFALLS B1/B2) -----------------------------------------------------------

bool CanQueryClient(int slot) {
	return g_mapGate.Ready() && slot >= 0 && slot < kMaxSlots;
}

// The standard client checks of PLUGIN_STANDARD §7.3 / PITFALLS C2: connected, in game, not the
// SourceTV bot. Bots are not excluded here: the original boosted every alive pawn, bots included.
bool IsInGameSlot(int slot) {
	if (!CanQueryClient(slot)) {
		return false;
	}
	if (__s2sdk_IsClientConnected == nullptr || __s2sdk_IsClientInGame == nullptr ||
		__s2sdk_IsClientSourceTV == nullptr) {
		return false;
	}
	try {
		return s2sdk::IsClientConnected(slot) && s2sdk::IsClientInGame(slot) && !s2sdk::IsClientSourceTV(slot);
	} catch (...) {
		return false;
	}
}

// A human player for the cookie reconcile: the checks above plus "not a bot". The permissions
// store has no entry for a bot (no SteamID64), and the HEAD port skipped fake clients there too.
bool IsHumanInGameSlot(int slot) {
	if (!IsInGameSlot(slot) || __s2sdk_IsFakeClient == nullptr) {
		return false;
	}
	try {
		return !s2sdk::IsFakeClient(slot);
	} catch (...) {
		return false;
	}
}

// The slot resolves to a live controller entity of a running map: the original's
// "CCSPlayerController::FromSlot(slot) != nullptr". The pointer, its handle and the entity index
// are all validated, because the generated wrappers call their slots unchecked (PITFALLS C2).
bool HasController(int slot) {
	if (!CanQueryClient(slot)) {
		return false;
	}
	if (__s2sdk_PlayerSlotToEntPointer == nullptr || __s2sdk_EntPointerToEntIndex == nullptr ||
		__s2sdk_PlayerSlotToEntHandle == nullptr || __s2sdk_IsValidEntHandle == nullptr) {
		return false;
	}
	try {
		void* controller = s2sdk::PlayerSlotToEntPointer(slot);
		if (controller == nullptr) {
			return false;
		}
		if (__s2sdk_IsValidEntPointer != nullptr && !s2sdk::IsValidEntPointer(controller)) {
			return false;
		}
		const int32_t handle = s2sdk::PlayerSlotToEntHandle(slot);
		// 0 and -1 are not controller handles; other negative values may be valid (PITFALLS C1).
		if (!roundend::IsValidEntityHandleValue(handle) || !s2sdk::IsValidEntHandle(handle)) {
			return false;
		}
		return s2sdk::EntPointerToEntIndex(controller) > 0;
	} catch (...) {
		return false;
	}
}

// The slot's CEntityHandle (index | serial << 15) when its controller is alive, else 0. The handle
// carries the entity's serial, so a reused slot gets a different one - the menu identity below
// compares it in addition to the SteamID64.
int32_t CurrentControllerHandle(int slot) {
	if (!HasController(slot)) {
		return 0;
	}
	try {
		const int32_t handle = s2sdk::PlayerSlotToEntHandle(slot);
		return roundend::IsValidEntityHandleValue(handle) && s2sdk::IsValidEntHandle(handle) ? handle : 0;
	} catch (...) {
		return 0;
	}
}

// s2sdk::IsClientAlive is the original's "pawn && pawn->IsAlive()".
bool IsAliveSlot(int slot) {
	if (!CanQueryClient(slot) || __s2sdk_IsClientAlive == nullptr) {
		return false;
	}
	try {
		return s2sdk::IsClientAlive(slot);
	} catch (...) {
		return false;
	}
}

// pisex::g_pPlayers->GetSteamID64(): 0 for a slot without a connected client (s2sdk keeps the
// previous occupant's id otherwise, which must not be compared).
uint64_t ClientSteamId64(int slot) {
	if (!CanQueryClient(slot) || __s2sdk_IsClientConnected == nullptr || __s2sdk_GetClientSteamID64 == nullptr) {
		return 0;
	}
	try {
		if (!s2sdk::IsClientConnected(slot)) {
			return 0;
		}
		return s2sdk::GetClientSteamID64(slot);
	} catch (...) {
		return 0;
	}
}

// --- output helpers ------------------------------------------------------------------------------

// s2sdk::PrintToChat behind the old pisex::PrintToChat guards (map ready, slot range, binding).
bool PrintChatLine(int slot, std::string_view message) {
	if (!CanQueryClient(slot) || __s2sdk_PrintToChat == nullptr) {
		return false;
	}
	try {
		s2sdk::PrintToChat(slot, plg::string(message));
		return true;
	} catch (...) {
		return false;
	}
}

// engine->ServerCommand(): s2sdk appends the newline itself, so the command bytes are the ones of
// the original without its explicit "\n".
void ServerCommand(const char* command) {
	if (__s2sdk_ServerCommand == nullptr) {
		return;
	}
	try {
		s2sdk::ServerCommand(plg::string(command));
	} catch (...) {
	}
}

// --- the config (the original's LoadConfig) ------------------------------------------------------

std::string HostConfigsRoot() {
	if (plg::GetConfigsDir == nullptr) {
		return {};
	}
	return ToStd(plg::GetConfigsDir());
}

std::string HostDataRoot() {
	if (plg::GetDataDir == nullptr) {
		return {};
	}
	return ToStd(plg::GetDataDir());
}

void LoadConfig() {
	const roundend::ConfigLoadResult result =
			roundend::LoadSettings(roundend::ConfigCandidates(HostConfigsRoot(), HostDataRoot()));
	if (!result.found) {
		LogError(std::string(kLogTag) + " failed to load " + result.path + " - keeping the built-in defaults");
		return;
	}
	if (!result.parsed) {
		LogError(std::string(kLogTag) + " failed to parse " + result.path + " - keeping the built-in defaults");
		return;
	}
	g_settings = result.settings;
}

// --- PITFALLS C8: map-gated raw schema offsets ---------------------------------------------------

// -1 while the gate is closed (GetSchemaOffset segfaults before the first map), and a failed
// lookup is not cached, so a field that was not available yet is retried instead of poisoned.
int32_t LookupSchemaOffset(const char* className, const char* fieldName) {
	if (!g_mapGate.Ready() || __s2sdk_GetSchemaOffset == nullptr) {
		return -1;
	}
	try {
		return s2sdk::GetSchemaOffset(plg::string(className), plg::string(fieldName));
	} catch (...) {
		return -1;
	}
}

void ResolveSchemaOffsets() {
	if (g_velocityOffset < 0) {
		g_velocityOffset = LookupSchemaOffset(kPawnClass, kVelocityField);
	}
	if (g_gravityOffset < 0) {
		g_gravityOffset = LookupSchemaOffset(kBaseEntityClass, kGravityField);
	}
	if (g_actualGravityOffset < 0) {
		g_actualGravityOffset = LookupSchemaOffset(kBaseEntityClass, kActualGravityField);
	}
	if (!g_loggedSchemaFailure &&
		(g_velocityOffset < 0 || g_gravityOffset < 0 || g_actualGravityOffset < 0)) {
		g_loggedSchemaFailure = true;
		std::string missing;
		if (g_velocityOffset < 0) {
			missing += std::string(kPawnClass) + "::" + kVelocityField + " ";
		}
		if (g_gravityOffset < 0) {
			missing += std::string(kBaseEntityClass) + "::" + kGravityField + " ";
		}
		if (g_actualGravityOffset < 0) {
			missing += std::string(kBaseEntityClass) + "::" + kActualGravityField + " ";
		}
		LogError(std::string(kLogTag) + " schema field(s) not found: " + missing +
				 "- the matching round-end write stays off");
	}
}

// The original's "CCSPlayerController::FromSlot(slot) -> m_hPlayerPawn, pawn && pawn->IsAlive()"
// reduced to the pawn's validated entity handle: s2sdk::IsClientAlive() is the pawn/alive part and
// s2sdk::GetClientPawn() is the controller's current pawn (m_hPawn; for an alive controller this is
// the same pawn m_hPlayerPawn() returned). 0 = no usable alive pawn (0 and -1 are invalid handle
// values; a valid negative handle is accepted, PITFALLS C1).
int32_t ResolveAlivePawn(int slot) {
	if (!IsInGameSlot(slot) || !HasController(slot) || !IsAliveSlot(slot)) {
		return 0;
	}
	if (__s2sdk_GetClientPawn == nullptr || __s2sdk_EntPointerToEntHandle == nullptr ||
		__s2sdk_IsValidEntHandle == nullptr) {
		return 0;
	}
	try {
		void* pawn = s2sdk::GetClientPawn(slot);
		if (pawn == nullptr) {
			return 0;
		}
		if (__s2sdk_IsValidEntPointer != nullptr && !s2sdk::IsValidEntPointer(pawn)) {
			return 0;
		}
		const int32_t handle = s2sdk::EntPointerToEntHandle(pawn);
		if (!roundend::IsValidEntityHandleValue(handle) || !s2sdk::IsValidEntHandle(handle)) {
			return 0;
		}
		return handle;
	} catch (...) {
		return 0;
	}
}

// One raw float write at a cached offset, without a network state change: the original assigned
// m_flVelocityModifier/m_flGravityScale/m_flActualGravityScale through hl2sdk without
// SetStateChanged (the HEAD port passed bChangeState=false for the same reason). chainOffset is
// only read when changeState is true, so -2 (the s2sdk sentinel for non-entity classes) is inert.
void ApplyPawnFloat(int32_t pawnHandle, int32_t offset, float value) {
	if (offset < 0 || !roundend::IsValidEntityHandleValue(pawnHandle) || __s2sdk_SetEntDataFloat == nullptr) {
		return;
	}
	try {
		s2sdk::SetEntDataFloat(pawnHandle, offset, value, 4, /*changeState=*/false, /*chainOffset=*/-2);
	} catch (...) {
	}
}

// --- the round-end behaviour (the original's OnRoundEnd/OnRoundStart bodies) ----------------------

void RoundEndSlot(int slot) {
	const int32_t pawn = ResolveAlivePawn(slot);
	if (pawn == 0) {
		return;
	}
	const roundend::PlayerSettings& player = g_players[static_cast<std::size_t>(slot)];

	// The original's exact order: velocity first, then the two gravity fields.
	const bool boostSpeed = g_settings.speedEnabled && !player.disableSpeed;
	ApplyPawnFloat(pawn, g_velocityOffset, boostSpeed ? g_settings.speed : roundend::kDefaultSpeed);

	const bool boostGravity = g_settings.gravityEnabled && !player.disableGravity;
	const float gravity = boostGravity ? g_settings.gravity : roundend::kDefaultGravity;
	ApplyPawnFloat(pawn, g_gravityOffset, gravity);
	ApplyPawnFloat(pawn, g_actualGravityOffset, gravity);
}

void RoundStartSlot(int slot) {
	const int32_t pawn = ResolveAlivePawn(slot);
	if (pawn == 0) {
		return;
	}
	ApplyPawnFloat(pawn, g_velocityOffset, roundend::kDefaultSpeed);
	ApplyPawnFloat(pawn, g_gravityOffset, roundend::kDefaultGravity);
	ApplyPawnFloat(pawn, g_actualGravityOffset, roundend::kDefaultGravity);
}

void OnRoundEnd() {
	// PITFALLS B2: round_end is still delivered around a changelevel; no client, entity or schema
	// call may happen while the gate is closed (the convar writes are skipped with it - they are
	// meaningless between maps).
	if (!g_mapGate.Ready()) {
		return;
	}
	if (g_settings.bhopEnabled) {
		ServerCommand("sv_autobunnyhopping 1");
		ServerCommand("sv_enablebunnyhopping 1");
	}
	ResolveSchemaOffsets();
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		try {
			RoundEndSlot(slot);
		} catch (const std::exception& ex) {
			LogError(std::string(kLogTag) + " round-end boost of slot " + std::to_string(slot) + " failed: " +
					 ex.what());
		} catch (...) {
			LogError(std::string(kLogTag) + " round-end boost of slot " + std::to_string(slot) + " failed");
		}
	}
}

void OnRoundStart() {
	if (!g_mapGate.Ready()) {
		return;
	}
	if (g_settings.bhopEnabled) {
		ServerCommand("sv_autobunnyhopping 0");
		ServerCommand("sv_enablebunnyhopping 0");
	}
	ResolveSchemaOffsets();
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		try {
			RoundStartSlot(slot);
		} catch (const std::exception& ex) {
			LogError(std::string(kLogTag) + " round-start restore of slot " + std::to_string(slot) +
					 " failed: " + ex.what());
		} catch (...) {
			LogError(std::string(kLogTag) + " round-start restore of slot " + std::to_string(slot) + " failed");
		}
	}
}

// --- permissions cookies (the HEAD port's cookie layer, now on the official permissions) ---------

// plg::Plugin::GetId() behind a null check: without the host's accessor there is no plugin id to
// attribute the write to, so the save is skipped instead of crashing on a null function pointer.
int64_t OwnPluginId() {
	if (plg::plugin::GetId == nullptr) {
		return -1;
	}
	try {
		return static_cast<int64_t>(plg::plugin::GetId(plg::plugin::handle));
	} catch (...) {
		return -1;
	}
}

// The HEAD port's TryLoadSettings: both cookies must be known (present or CookieNotFound) before
// the slot counts as loaded; TargetUserNotFound / DBNotReady are retried by the reconcile.
//
// A feature with a pending save was changed by the player in the menu: the in-memory value is
// authoritative and the stored one must not overwrite it. The *other* feature is still merged in,
// so a toggle made before the permissions user was loaded never erases the stored other cookie.
void TryLoadSettings(int slot) {
	if (!g_permissionsAvailable || __permissions_GetCookie == nullptr || slot < 0 || slot >= kMaxSlots) {
		return;
	}
	SlotState& state = g_slots[static_cast<std::size_t>(slot)];
	if (state.steamid == 0 || state.loaded) {
		return;
	}
	const bool applySpeed = (state.pendingFeatures & kPendingSpeed) == 0;
	const bool applyGravity = (state.pendingFeatures & kPendingGravity) == 0;
	try {
		bool disable = false;
		plg::any value;
		const permissions::Status speedStatus = permissions::GetCookie(state.steamid, plg::string(kCookieSpeed), value);
		if (speedStatus == permissions::Status::Success) {
			if (!applySpeed) {
				// the player's pending choice wins over the stored value
			} else if (roundend::CookieValueToBool(value, disable)) {
				g_players[static_cast<std::size_t>(slot)].disableSpeed = disable;
			} else {
				LogWarn(std::string(kLogTag) + " user " + std::to_string(state.steamid) + " has an unusable \"" +
						kCookieSpeed + "\" cookie - keeping the default");
			}
		} else if (speedStatus != permissions::Status::CookieNotFound) {
			return; // TargetUserNotFound / DBNotReady: retried by the reconcile
		}

		const permissions::Status gravityStatus =
				permissions::GetCookie(state.steamid, plg::string(kCookieGravity), value);
		if (gravityStatus == permissions::Status::Success) {
			if (!applyGravity) {
				// the player's pending choice wins over the stored value
			} else if (roundend::CookieValueToBool(value, disable)) {
				g_players[static_cast<std::size_t>(slot)].disableGravity = disable;
			} else {
				LogWarn(std::string(kLogTag) + " user " + std::to_string(state.steamid) + " has an unusable \"" +
						kCookieGravity + "\" cookie - keeping the default");
			}
		} else if (gravityStatus != permissions::Status::CookieNotFound) {
			return;
		}

		// Both cookies are known (present or absent): the slot is loaded.
		state.loaded = true;
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " reading the round-end cookies failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " reading the round-end cookies failed with an unknown exception");
	}
}

void RefreshSlot(int slot, uint64_t steamid);

// The HEAD port's TrySaveSettings: writes both cookies, keeps pendingSave while the store is not
// ready, and refuses a stale save whose slot already changed hands (the new occupant never asked
// for that change).
void TrySaveSettings(int slot) {
	if (!g_permissionsAvailable || __permissions_SetCookie == nullptr || slot < 0 || slot >= kMaxSlots) {
		return;
	}
	SlotState& state = g_slots[static_cast<std::size_t>(slot)];
	if (state.steamid == 0 || !state.pendingSave) {
		return;
	}
	// The pending save belongs to the player, not to the slot: the reconcile refreshes a slot only
	// once a second, so a menu click made right after the slot changed hands still carries the
	// previous occupant's settings and would be written into that player's stored cookie. Refresh
	// the slot instead (which drops the stale save) - the new occupant never asked for the change.
	// A slot with no connected client has no owner the pending choice can be attributed to either:
	// writing it would store the previous occupant's pending state under their SteamID64 after they
	// are gone, so it is dropped as well.
	const uint64_t currentSteamId = ClientSteamId64(slot);
	if (currentSteamId == 0) {
		state.pendingSave = false;
		state.pendingFeatures = 0;
		return;
	}
	if (currentSteamId != state.steamid) {
		RefreshSlot(slot, currentSteamId);
		return;
	}
	const int64_t pluginId = OwnPluginId();
	if (pluginId < 0) {
		return; // no host accessor: the save cannot be attributed, the reconcile retries
	}
	try {
		const roundend::PlayerSettings& settings = g_players[static_cast<std::size_t>(slot)];
		const permissions::Status speedStatus = permissions::SetCookie(
				pluginId, state.steamid, plg::string(kCookieSpeed),
				plg::any(static_cast<int64_t>(settings.disableSpeed ? 1 : 0)), /*dontBroadcast=*/false);
		if (speedStatus != permissions::Status::Success) {
			return; // TargetUserNotFound (user not loaded yet) / DBNotReady: retried later
		}
		const permissions::Status gravityStatus = permissions::SetCookie(
				pluginId, state.steamid, plg::string(kCookieGravity),
				plg::any(static_cast<int64_t>(settings.disableGravity ? 1 : 0)), /*dontBroadcast=*/false);
		if (gravityStatus != permissions::Status::Success) {
			return;
		}
		state.pendingSave = false;
		state.pendingFeatures = 0;
		state.loaded = true;
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " saving the round-end cookies failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " saving the round-end cookies failed with an unknown exception");
	}
}

// The HEAD port's MarkPendingSave: queue the write and try it immediately (the reconcile retries
// while the store is not ready). The chosen feature is remembered so a later load merges only the
// other cookie instead of overwriting the player's choice.
void MarkPendingSave(int slot, roundend::MenuChoice choice) {
	if (slot < 0 || slot >= kMaxSlots) {
		return;
	}
	SlotState& state = g_slots[static_cast<std::size_t>(slot)];
	state.pendingSave = true;
	state.pendingFeatures |= choice == roundend::MenuChoice::Speed ? kPendingSpeed : kPendingGravity;
	TrySaveSettings(slot);
}

// The HEAD port's RefreshSlot: a slot that changed hands starts from the defaults until the new
// player's cookies are loaded.
void RefreshSlot(int slot, uint64_t steamid) {
	if (slot < 0 || slot >= kMaxSlots) {
		return;
	}
	SlotState& state = g_slots[static_cast<std::size_t>(slot)];
	if (state.steamid != steamid) {
		state = SlotState{};
		state.steamid = steamid;
		g_players[static_cast<std::size_t>(slot)] = roundend::PlayerSettings{};
	}
	TryLoadSettings(slot);
}

// The permissions OnUserLoaded body: refresh/save the slots owning the steamid.
void OnUserLoaded(uint64_t targetId) {
	if (targetId == 0) {
		return;
	}
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		if (g_slots[static_cast<std::size_t>(slot)].steamid == targetId) {
			TryLoadSettings(slot);
			TrySaveSettings(slot);
		}
	}
}

void ReconcileSlots() {
	if (!g_mapGate.Ready() || !g_permissionsAvailable) {
		return;
	}
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		try {
			// The HEAD port's IsConnected && IsAuthenticated && !IsFakeClient; the standard client
			// checks of PITFALLS C2 plus the human test, and a real SteamID64.
			if (!IsHumanInGameSlot(slot)) {
				continue;
			}
			const uint64_t steamid = ClientSteamId64(slot);
			if (steamid == 0) {
				continue;
			}
			RefreshSlot(slot, steamid);
			TrySaveSettings(slot);
		} catch (const std::exception& ex) {
			LogError(std::string(kLogTag) + " reconcile of slot " + std::to_string(slot) + " failed: " + ex.what());
		} catch (...) {
			LogError(std::string(kLogTag) + " reconcile of slot " + std::to_string(slot) + " failed");
		}
	}
}

// --- the menu (the original's OpenRoundEndSettingsMenu) ------------------------------------------
//
// The rules of PITFALLS C10 and of s2sdk's menu manager, all enforced here:
//   * a menu handler runs with the menu manager's lock held: it only records the selection. The
//     toggle, the chat reply, the cookie write and the re-open are applied from OnPluginUpdate,
//     after the slot's full owner identity (SteamID64 + controller handle/serial captured when the
//     menu was opened) has been re-checked - a slot reused in that window can never receive the
//     previous player's change, message or cookie;
//   * s2sdk never destroys a closed menu: every instance this plugin creates ends up in
//     g_destroyQueue and is destroyed outside the handler (OnPluginUpdate / OnPluginEnd);
//   * the manager's client state uses MenuId 0 as "no menu" (menu_manager.cpp:507/538/770), so a
//     displayable menu with id 0 could be shown but never cancelled, selected or destroyed. Id 0
//     is burned at start (pisexcompat's BurnMenuIdZero), therefore every menu of this plugin has
//     an id >= 1 and the sentinel can never be confused with one of ours;
//   * a menu is only created/displayed for an owner identity that can be re-checked exactly: a
//     nonzero SteamID64 and a valid nonzero controller CEntityHandle (0/-1 are invalid, a negative
//     handle with a real serial is valid). Missing bindings or a controller-less slot fail soft
//     with no menu. The selection is applied - and the menu re-opened - only while the same
//     SteamID64 *and* the same controller handle/serial are still in the slot, so a same-SteamID
//     reconnect with a new controller entity never inherits the previous selection;
//   * a menu still displayed when the map goes down is ended with
//     CancelClientMenu(slot, MenuCancelReason::Disconnect) first: EndClientMenu() calls
//     CloseClientDisplay(slot, reason != Disconnect), so the Disconnect reason skips the backend's
//     close callback (hudmenu's HideForSlot, PrintHtmlCentre, ...) that would touch the torn-down
//     client layer. The DestroyMenu that follows then has no viewer left and makes no backend call.

void OnMenuAction(s2sdk::MenuId id, s2sdk::MenuAction action, int32_t slot, int32_t param);

bool MenuBindingsAvailable() {
	return __s2sdk_CreateMenu != nullptr && __s2sdk_DestroyMenu != nullptr && __s2sdk_AddMenuItem != nullptr &&
		   __s2sdk_DisplayMenu != nullptr && __s2sdk_GetClientMenu != nullptr &&
		   __s2sdk_CancelClientMenu != nullptr && __s2sdk_SetMenuType != nullptr &&
		   __s2sdk_GetMenuType != nullptr;
}

// Reserves s2sdk's unusable MenuId 0 exactly like pisexcompat did: create one menu at start. If the
// answer is not 0 another plugin already consumed the id, so this one is destroyed again; if it is
// 0 the dummy stays allocated until OnPluginEnd (ReleaseReservedMenuId), because destroying it now
// would free the very id the manager must keep reserved for the process.
void BurnReservedMenuId() {
	if (g_reservedMenuIdZero || !MenuBindingsAvailable()) {
		return;
	}
	try {
		const s2sdk::MenuId id = s2sdk::CreateMenu(plg::string(kReservedMenuTitle), &OnMenuAction, plg::string());
		if (id != 0) {
			try {
				s2sdk::DestroyMenu(id); // id 0 was already taken elsewhere: nothing to reserve
			} catch (...) {
			}
			return;
		}
		g_reservedMenuIdZero = true;
	} catch (...) {
	}
}

// Called from OnPluginEnd: the dummy menu's handler is a function pointer into this library, which
// is about to be unloaded. Nobody can display id 0 (the manager's client state treats 0 as "no
// menu"), so this destroys a menu with no viewer and makes no backend call - safe outside a map.
void ReleaseReservedMenuId() {
	if (!g_reservedMenuIdZero) {
		return;
	}
	g_reservedMenuIdZero = false;
	if (__s2sdk_DestroyMenu == nullptr) {
		return;
	}
	try {
		s2sdk::DestroyMenu(0);
	} catch (...) {
	}
}

bool IsMenuTypeRegisteredSafe(const char* name) {
	if (__s2sdk_IsMenuTypeRegistered == nullptr) {
		return false;
	}
	try {
		return s2sdk::IsMenuTypeRegistered(plg::string(name));
	} catch (...) {
		return false;
	}
}

// The player's own "!menu" choice, asked from the optional hudmenu plugin (its
// __hudmenu_GetClientMenuType export); "" when hudmenu is not installed or has no answer.
std::string PlayerMenuTypeChoice(int slot) {
	if (__hudmenu_GetClientMenuType == nullptr) {
		return {};
	}
	try {
		return ToStd(hudmenu::GetClientMenuType(slot));
	} catch (...) {
		return {};
	}
}

// pisexcompat's ResolveS2sdkMenuTypeName() for the original's default HUD_LAYOUT type: the player's
// hudmenu choice wins when it names a known backend, and the default is the "hud" Panorama backend
// when hudmenu registered it, else s2sdk's on-screen WASD "button" (libs/pisexcompat/src/menu.cpp).
// A "hud"/"wasd" choice whose backend is down falls back to "button", exactly like pisexcompat.
std::string ResolveMenuType(int slot) {
	const std::string choice = PlayerMenuTypeChoice(slot);
	if (choice == kChatMenuType || choice == kCenterHtmlMenuType || choice == kButtonMenuType) {
		return choice;
	}
	if (choice == kHudMenuType) {
		return IsMenuTypeRegisteredSafe(kHudMenuType) ? std::string(kHudMenuType) : std::string(kButtonMenuType);
	}
	if (choice == kWasdMenuType) {
		return IsMenuTypeRegisteredSafe(kWasdMenuType) ? std::string(kWasdMenuType) : std::string(kButtonMenuType);
	}
	return IsMenuTypeRegisteredSafe(kHudMenuType) ? std::string(kHudMenuType) : std::string(kButtonMenuType);
}

void EnqueueMenuDestroy(int slot, s2sdk::MenuId id) {
	if (id == 0) {
		return; // the reserved sentinel is released by ReleaseReservedMenuId, never displayed
	}
	for (const MenuDestroyRequest& queued : g_destroyQueue) {
		if (queued.id == id) {
			return;
		}
	}
	g_destroyQueue.push_back(MenuDestroyRequest{slot, id});
}

void ClearSlotMenuState(SlotMenu& menu) {
	menu.handle.reset();
	menu.reopenPending = false;
	menu.choicePending = false;
	menu.pendingChoice = roundend::MenuChoice::None;
	menu.steamid = 0;
	menu.controllerHandle = 0;
	menu.itemChoices = {};
	menu.itemCount = 0;
}

// The slot that currently displays `id`, -1 when nobody does. MenuId 0 is s2sdk's "no menu"
// sentinel and is never a display this plugin created.
int FindDisplaySlot(s2sdk::MenuId id) {
	if (id == 0 || __s2sdk_GetClientMenu == nullptr) {
		return -1;
	}
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		try {
			if (s2sdk::GetClientMenu(slot) == id) {
				return slot;
			}
		} catch (...) {
		}
	}
	return -1;
}

// Replaces the menu's type with a builtin whose close() is a no-op ("chat" / "console",
// menu_types_builtin.cpp: ChatMenu_Close / ConsoleMenu_Close), so a DestroyMenu that still has to
// end a display cannot reach the backend (PrintHtmlCentre, hudmenu's HideForSlot, ...) while the
// client layer is being torn down. Verified through GetMenuType; both bindings are part of the
// required menu set, so the fail-safe always has them.
bool MakeMenuCloseNoOp(s2sdk::MenuId id) {
	if (__s2sdk_SetMenuType == nullptr || __s2sdk_GetMenuType == nullptr) {
		return false;
	}
	for (const char* type : {kChatMenuType, kConsoleMenuType}) {
		if (__s2sdk_IsMenuTypeRegistered != nullptr) {
			bool registered = false;
			try {
				registered = s2sdk::IsMenuTypeRegistered(plg::string(type));
			} catch (...) {
				registered = false;
			}
			if (!registered) {
				continue;
			}
		}
		try {
			if (!s2sdk::SetMenuType(id, plg::string(type))) {
				continue;
			}
			if (ToStd(s2sdk::GetMenuType(id)) == type) {
				return true;
			}
		} catch (...) {
		}
	}
	return false;
}

// Ends every display of `id` and destroys the menu; the caller must not be inside a menu handler.
//   * CancelClientMenu(slot, Disconnect) skips the backend's close callback (menu_manager.cpp
//     CloseClientDisplay(slot, reason != Disconnect)); a false answer means "the display already
//     ended", which is re-checked with GetClientMenu;
//   * if a display is somehow still there in a map-down window (an s2sdk contract violation), the
//     menu is switched to the no-op-close builtin type before DestroyMenu - safe between maps and
//     the menu, with this library's handler, cannot survive the unload;
//   * `mapReady == true` may end a viewer through the backend (legal with live clients).
// Returns true when the id is gone from s2sdk's menu table.
bool DestroyMenuChecked(s2sdk::MenuId id, bool mapReady) {
	if (__s2sdk_DestroyMenu == nullptr) {
		return false;
	}
	if (id == 0) {
		// The reserved id: client state 0 is s2sdk's "no menu", so it can never be displayed.
		try {
			return s2sdk::DestroyMenu(0);
		} catch (...) {
			return false;
		}
	}
	const int display = FindDisplaySlot(id);
	if (display >= 0) {
		bool cancelled = false;
		if (__s2sdk_CancelClientMenu != nullptr) {
			try {
				cancelled = s2sdk::CancelClientMenu(display, s2sdk::MenuCancelReason::Disconnect);
			} catch (...) {
				cancelled = false;
			}
		}
		if (!cancelled && FindDisplaySlot(id) >= 0 && !mapReady && !MakeMenuCloseNoOp(id)) {
			return false; // would need a backend close over the torn-down client layer
		}
	}
	bool destroyed = false;
	try {
		destroyed = s2sdk::DestroyMenu(id);
	} catch (...) {
		destroyed = false;
	}
	if (__s2sdk_IsValidMenu != nullptr) {
		// Authoritative check: false from DestroyMenu also means "the id was already gone", which
		// counts as released - only an id that is still a valid menu is unresolved.
		try {
			return !s2sdk::IsValidMenu(id);
		} catch (...) {
		}
	}
	return destroyed && FindDisplaySlot(id) < 0;
}

// Drains the deferred destroy requests outside the menu manager's lock.
//   finalUnload == false: a request that cannot be released yet (map-down fail-safe failed, or an
//   s2sdk contract violation) is kept and retried on a later frame / the next map;
//   finalUnload == true (OnPluginEnd): the ids that still cannot be released are reported as
//   A7_UNRESOLVED - their handler points into this library and the plugin API cannot remove it, so
//   this is never silently treated as cleaned up.
void DrainDestroyQueue(bool mapReady, bool finalUnload) {
	if (g_destroyQueue.empty()) {
		return;
	}
	std::vector<MenuDestroyRequest> pending;
	pending.swap(g_destroyQueue);
	std::vector<s2sdk::MenuId> unresolved;
	for (const MenuDestroyRequest& request : pending) {
		if (DestroyMenuChecked(request.id, mapReady)) {
			continue;
		}
		if (!finalUnload) {
			LogWarn(std::string(kLogTag) + " menu " + std::to_string(request.id) + " on slot " +
					std::to_string(request.slot) +
					" is still displayed - retrying the destroy on a later frame");
			g_destroyQueue.push_back(request);
		} else {
			unresolved.push_back(request.id);
		}
	}
	if (!unresolved.empty()) {
		std::string list;
		for (const s2sdk::MenuId id : unresolved) {
			if (!list.empty()) {
				list += ", ";
			}
			list += std::to_string(id);
		}
		LogError(std::string(kLogTag) + " A7_UNRESOLVED: s2sdk refused to release menu id(s) " + list +
				 " - their handler points into this library and cannot be removed by the plugin API. "
				 "This is an s2sdk MenuManager contract violation; do not deploy until it is understood");
	}
}

// Map end / unload between maps: no client or entity call may happen here, so every display is
// ended with the Disconnect reason (no backend close) and every menu instance this plugin created
// is freed while it has no viewer left. After this nothing in s2sdk points at this library.
void ReleaseAllMenusForMapTeardown() {
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		SlotMenu& menu = g_menus[static_cast<std::size_t>(slot)];
		const std::optional<s2sdk::MenuId> handle = menu.handle;
		ClearSlotMenuState(menu);
		if (handle) {
			EnqueueMenuDestroy(slot, *handle);
		}
	}
	DrainDestroyQueue(/*mapReady=*/false, /*finalUnload=*/false);
}

// Used by OnPluginEnd (map up) and as the defensive path of OnMapStart for a menu set whose
// OnMapEnd was never delivered. The checked destruction still ends a display with the Disconnect
// reason first; with a live map a backend close is legal, so it may run only if the manager refuses
// that teardown (the contract-violation branch of DestroyMenuChecked).
void ReleaseAllMenusWithLiveMap() {
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		SlotMenu& menu = g_menus[static_cast<std::size_t>(slot)];
		const std::optional<s2sdk::MenuId> handle = menu.handle;
		ClearSlotMenuState(menu);
		if (handle) {
			EnqueueMenuDestroy(slot, *handle);
		}
	}
	DrainDestroyQueue(/*mapReady=*/true, /*finalUnload=*/false);
}

// OnPluginEnd: every menu goes through the checked destruction (the Disconnect teardown first); a
// backend close can only happen in the live-map contract-violation branch. Unresolved ids are
// reported as A7_UNRESOLVED. The reserved id 0 is released last.
void DestroyAllMenusBestEffort() {
	if (g_mapGate.Ready()) {
		ReleaseAllMenusWithLiveMap();
	} else {
		ReleaseAllMenusForMapTeardown();
		if (!g_destroyQueue.empty()) {
			// The library is going away: try the checked destruction one last time and report what
			// cannot be released as A7_UNRESOLVED instead of pretending it was cleaned up.
			DrainDestroyQueue(/*mapReady=*/false, /*finalUnload=*/true);
		}
	}
	ReleaseReservedMenuId();
}

// The identity captured when the menu opened, re-checked when the recorded intent is applied or the
// menu is re-opened: the slot must still hold the same connected player (SteamID64) *and* the same
// controller entity (handle/serial). Both were required to be nonzero before the menu was created,
// so this is always an exact comparison - a same-SteamID reconnect with a new controller entity is
// a different owner and drops the intent.
bool MenuOwnerStillThere(int slot, const SlotMenu& menu) {
	if (!g_mapGate.Ready() || !IsInGameSlot(slot) || menu.steamid == 0 || menu.controllerHandle == 0) {
		return false;
	}
	const uint64_t steamid = ClientSteamId64(slot);
	if (steamid == 0 || steamid != menu.steamid) {
		return false;
	}
	const int32_t controller = CurrentControllerHandle(slot);
	return controller != 0 && controller == menu.controllerHandle;
}

// The owner identity a menu needs: a nonzero SteamID64 and a valid, nonzero controller CEntityHandle
// (0/-1 are the invalid values; a negative handle with a real serial is valid, PITFALLS C1). The
// controller handle carries the entity's serial: the same SteamID64 reconnecting into the slot is a
// new controller entity and must not inherit the previous menu selection. Missing bindings or a
// controller-less slot fail soft here - no menu is created at all.
bool MenuOwnerIdentity(int slot, uint64_t& steamid, int32_t& controllerHandle) {
	steamid = 0;
	controllerHandle = 0;
	if (!IsInGameSlot(slot)) {
		return false;
	}
	steamid = ClientSteamId64(slot);
	if (steamid == 0) {
		return false;
	}
	controllerHandle = CurrentControllerHandle(slot);
	return controllerHandle != 0;
}

// Applies the selection recorded by the handler (see SlotMenu): never inside the menu manager's
// lock, never for a slot that changed hands. The owner identity is compared exactly (SteamID64 and
// the controller handle/serial); a missing or different owner drops the intent with no toggle, no
// chat line, no cookie write and no re-open.
void ApplyPendingChoices() {
	if (g_tearingDown || !g_mapGate.Ready()) {
		return;
	}
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		SlotMenu& menu = g_menus[static_cast<std::size_t>(slot)];
		if (!menu.choicePending) {
			continue;
		}
		menu.choicePending = false;
		const roundend::MenuChoice choice = menu.pendingChoice;
		menu.pendingChoice = roundend::MenuChoice::None;
		if (choice == roundend::MenuChoice::None || !MenuOwnerStillThere(slot, menu)) {
			continue;
		}
		const char* reply = roundend::ApplyChoice(g_players[static_cast<std::size_t>(slot)], choice);
		if (reply != nullptr) {
			PrintChatLine(slot, reply);
		}
		MarkPendingSave(slot, choice);
		menu.reopenPending = true; // the original re-opened the menu from its callback
	}
}

void OnMenuAction(s2sdk::MenuId id, s2sdk::MenuAction action, int32_t slot, int32_t param) {
	if (g_tearingDown || slot < 0 || slot >= kMaxSlots) {
		return;
	}
	SlotMenu& menu = g_menus[static_cast<std::size_t>(slot)];
	if (!menu.handle || *menu.handle != id) {
		return; // a stale callback of a menu this slot no longer owns
	}
	switch (action) {
	case s2sdk::MenuAction::Select:
		// The handler runs under the menu manager's lock: only remember what was picked. The
		// settings change, the reply, the cookie and the re-open are deferred to OnPluginUpdate,
		// after the owner identity has been re-checked (see SlotMenu).
		if (param < 0 || param >= menu.itemCount) {
			return;
		}
		if (menu.itemChoices[static_cast<std::size_t>(param)] == roundend::MenuChoice::None) {
			return;
		}
		menu.pendingChoice = menu.itemChoices[static_cast<std::size_t>(param)];
		menu.choicePending = true;
		break;
	case s2sdk::MenuAction::Cancel:
		// Exit / timeout / disconnect / interrupted: nothing to apply, the End that follows
		// destroys the menu. A pending re-open or selection is dropped with the real cancellation.
		menu.choicePending = false;
		menu.reopenPending = false;
		break;
	case s2sdk::MenuAction::End:
		// Close-on-select already ended the display, so destroying the instance is now safe; the
		// actual DestroyMenu call is deferred to OnPluginUpdate (PITFALLS C10).
		if (menu.handle && *menu.handle == id) {
			EnqueueMenuDestroy(slot, id);
			menu.handle.reset();
		}
		break;
	case s2sdk::MenuAction::Start:
		break;
	}
}

void OpenSettingsMenu(int slot) {
	if (!g_mapGate.Ready() || slot < 0 || slot >= kMaxSlots) {
		return;
	}
	if (!IsInGameSlot(slot)) {
		return; // no menu for a slot without a connected, in-game, non-SourceTV client
	}
	if (!MenuBindingsAvailable()) {
		if (!g_loggedMenuUnavailable) {
			g_loggedMenuUnavailable = true;
			LogError(std::string(kLogTag) + " the s2sdk menu bindings are unavailable (Create/Destroy/Display/"
												"AddItem/GetClientMenu/CancelClientMenu/SetMenuType/GetMenuType) - mm_ret/!ret stay inert");
		}
		return;
	}
	// The menu's owner identity is captured before anything is created: a slot without a nonzero
	// SteamID64 or a usable controller handle (missing accessor, invalid controller, 0/-1 handle)
	// gets no menu, so a selection can always be checked against the exact owner later.
	uint64_t steamid = 0;
	int32_t controllerHandle = 0;
	if (!MenuOwnerIdentity(slot, steamid, controllerHandle)) {
		if (!g_loggedMissingIdentity) {
			g_loggedMissingIdentity = true;
			LogWarn(std::string(kLogTag) +
					" the slot has no usable SteamID64/controller identity - the round-end menu stays closed");
		}
		return;
	}
	SlotMenu& menu = g_menus[static_cast<std::size_t>(slot)];
	if (menu.handle) {
		// A second open before the first menu ended: abandon the old instance; the deferred destroy
		// below removes it after the handler returns (a still-displayed session is ended with the
		// Disconnect reason by the map-down drain, and with DestroyMenu on a live map).
		EnqueueMenuDestroy(slot, *menu.handle);
		menu.handle.reset();
	}
	menu.reopenPending = false;
	// Load the stored cookies *before* the menu is built: the original loaded them from its
	// HookClientCookieLoaded callback well before a player could open the menu, and the HEAD port's
	// once-a-second reconcile could leave a freshly joined player's slot with steamid 0 - a toggle
	// in that window would then be overwritten by the first reconcile's load.
	RefreshSlot(slot, steamid);
	menu.steamid = steamid;
	menu.controllerHandle = controllerHandle;

	s2sdk::MenuId id = 0;
	try {
		id = s2sdk::CreateMenu(plg::string(roundend::kMenuTitle), &OnMenuAction,
							   plg::string(ResolveMenuType(slot)));
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " creating the round-end menu failed: " + ex.what());
		return;
	} catch (...) {
		LogError(std::string(kLogTag) + " creating the round-end menu failed");
		return;
	}
	if (id == 0) {
		// Id 0 is burned at start, so this cannot be displayed (the manager would treat it as "no
		// menu"): refuse it instead of registering an unusable handle. The instance that was just
		// created is erased right away - it has no viewer, so this makes no backend call and keeps
		// no handler of this library in s2sdk's menu table.
		LogError(std::string(kLogTag) + " s2sdk returned the unusable menu id 0 - the menu is not displayed");
		if (__s2sdk_DestroyMenu != nullptr) {
			try {
				s2sdk::DestroyMenu(0);
			} catch (...) {
			}
		}
		return;
	}

	menu.itemCount = 0;
	const roundend::PlayerSettings& player = g_players[static_cast<std::size_t>(slot)];
	if (g_settings.speedEnabled) {
		try {
			s2sdk::AddMenuItem(id, plg::string(roundend::kSpeedInfo), plg::string(roundend::SpeedMenuLabel(player)),
							   s2sdk::MenuItemStyle::Default);
			menu.itemChoices[static_cast<std::size_t>(menu.itemCount++)] = roundend::MenuChoice::Speed;
		} catch (...) {
		}
	}
	if (g_settings.gravityEnabled) {
		try {
			s2sdk::AddMenuItem(id, plg::string(roundend::kGravityInfo),
							   plg::string(roundend::GravityMenuLabel(player)), s2sdk::MenuItemStyle::Default);
			menu.itemChoices[static_cast<std::size_t>(menu.itemCount++)] = roundend::MenuChoice::Gravity;
		} catch (...) {
		}
	}
	if (__s2sdk_SetMenuExitButton != nullptr) {
		try {
			s2sdk::SetMenuExitButton(id, true); // the original's SetExitMenu(menu, true)
		} catch (...) {
		}
	}
	if (__s2sdk_SetMenuCloseOnSelect != nullptr) {
		try {
			// The original's pisex menu closed the display when an item was picked and the callback
			// re-opened it; the s2sdk default is already true, this makes the contract explicit.
			s2sdk::SetMenuCloseOnSelect(id, true);
		} catch (...) {
		}
	}

	bool displayed = false;
	try {
		displayed = s2sdk::DisplayMenu(id, slot, 0); // no timeout, like the original
	} catch (...) {
		displayed = false;
	}
	if (!displayed) {
		EnqueueMenuDestroy(slot, id);
		return;
	}
	menu.handle = id;
}

// Runs from OnPluginUpdate, outside the menu manager's lock: frees the closed sessions and re-opens
// the menu of a successful selection, but only while the slot still holds the same player.
void ProcessMenuWork() {
	if (g_tearingDown || !g_mapGate.Ready()) {
		return; // a DestroyMenu between maps would call the backend's close on torn-down clients
	}
	DrainDestroyQueue(/*mapReady=*/true, /*finalUnload=*/false);
	for (int slot = 0; slot < kMaxSlots; ++slot) {
		SlotMenu& menu = g_menus[static_cast<std::size_t>(slot)];
		if (!menu.reopenPending) {
			continue;
		}
		menu.reopenPending = false;
		if (!MenuOwnerStillThere(slot, menu)) {
			continue; // the slot changed hands between the selection and this update
		}
		OpenSettingsMenu(slot);
	}
}

// --- commands (PITFALLS C11) ---------------------------------------------------------------------

void AttachCommand(CommandRegistration& command) {
	if (command.attempted || command.callback == nullptr) {
		return;
	}
	// The ownership of the name is only known after AddConsoleCommand() (true = created here,
	// false = an existing entry with our listener attached), so both removers are part of the
	// required pair: without the one teardown would need, the callback could outlive the unload.
	if (__s2sdk_AddConsoleCommand == nullptr || __s2sdk_RemoveCommand == nullptr ||
		__s2sdk_RemoveCommandListener == nullptr) {
		LogError(std::string(kLogTag) +
				 " the s2sdk command bindings are unavailable (AddConsoleCommand/RemoveCommand/"
				 "RemoveCommandListener) - '" + command.name + "' is not registered");
		return;
	}
	try {
		command.owned = s2sdk::AddConsoleCommand(plg::string(std::string_view(command.name)), plg::string(),
												command.flags, command.callback, s2sdk::HookMode::Pre);
		command.attempted = true;
	} catch (...) {
		LogError(std::string(kLogTag) + " AddConsoleCommand failed for '" + command.name + "'");
		return;
	}
	if (!command.owned) {
		// false = the name already existed (engine or another plugin) and s2sdk attached our
		// callback as a listener: the command keeps working through it, and OnPluginEnd must
		// detach only that listener (PITFALLS C11).
		LogInfo(std::string(kLogTag) + " '" + command.name +
				"' already exists; reusing the entry (the listener is attached)");
	}
}

void DetachCommand(CommandRegistration& command) {
	if (!command.attempted) {
		return;
	}
	command.attempted = false;
	if (command.owned) {
		// This plugin created the command: RemoveCommand detaches the listener and the command.
		if (__s2sdk_RemoveCommand != nullptr) {
			try {
				s2sdk::RemoveCommand(plg::string(std::string_view(command.name)), command.callback);
			} catch (...) {
			}
		}
	} else if (__s2sdk_RemoveCommandListener != nullptr) {
		// The name already existed: only the listener is ours. RemoveCommand would unregister
		// somebody else's command (PITFALLS C11).
		try {
			s2sdk::RemoveCommandListener(plg::string(std::string_view(command.name)), command.callback,
										s2sdk::HookMode::Pre);
		} catch (...) {
		}
	}
	command.owned = false;
}

void RegisterCommands() {
	for (CommandRegistration& command : g_commands) {
		AttachCommand(command);
	}
}

void RemoveCommands() {
	for (CommandRegistration& command : g_commands) {
		DetachCommand(command);
	}
}

// "mm_ret": the console spelling of the original. Only the console context is answered - a chat
// "!mm_ret" is not one of the original's chat spellings and stays an ordinary say line (C11). The
// server console (-1) is not a player slot; the original's callback had no menu for it either.
s2sdk::ResultType OnMmRetCommand(int32_t caller, s2sdk::ConCommandContext context,
								 const plg::vector<plg::string>& /*args*/) {
	if (context != s2sdk::ConCommandContext::Console) {
		return s2sdk::ResultType::Continue;
	}
	try {
		OpenSettingsMenu(caller);
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " 'mm_ret' failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " 'mm_ret' failed with an unknown exception");
	}
	return s2sdk::ResultType::Handled;
}

// "ret": the original's chat spelling ("!ret"/"/ret" chain to the trigger-less name, and s2sdk
// marks the call ConCommandContext::Chat). A console "ret" is not ours: it answers Continue and
// behaves exactly as if this plugin were not loaded (PITFALLS C11) - the console spelling is
// "mm_ret". The chat line is ours whatever the handler did, so it is never broadcast.
s2sdk::ResultType OnRetCommand(int32_t caller, s2sdk::ConCommandContext context,
							   const plg::vector<plg::string>& /*args*/) {
	if (context != s2sdk::ConCommandContext::Chat) {
		return s2sdk::ResultType::Continue;
	}
	try {
		OpenSettingsMenu(caller);
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " '!ret' failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " '!ret' failed with an unknown exception");
	}
	return s2sdk::ResultType::Handled;
}

// --- events (PITFALLS C12) -----------------------------------------------------------------------

// A Pre hook answers Continue on every path - a result >= Handled would free the event for the
// engine and every other plugin.
s2sdk::ResultType OnRoundEndEvent(const plg::string& /*name*/, void* /*event*/, bool /*dontBroadcast*/) {
	try {
		OnRoundEnd();
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " round_end failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " round_end failed with an unknown exception");
	}
	return s2sdk::ResultType::Continue;
}

s2sdk::ResultType OnRoundStartEvent(const plg::string& /*name*/, void* /*event*/, bool /*dontBroadcast*/) {
	try {
		OnRoundStart();
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " round_start failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " round_start failed with an unknown exception");
	}
	return s2sdk::ResultType::Continue;
}

bool HookOne(const char* name, s2sdk::EventCallback callback) {
	if (__s2sdk_HookEvent == nullptr) {
		LogError(std::string(kLogTag) + " the s2sdk event bindings are unavailable - '" + name + "' is not hooked");
		return false;
	}
	try {
		if (s2sdk::HookEvent(plg::string(name), callback, s2sdk::HookMode::Pre) == s2sdk::EventHookError::Okay) {
			return true;
		}
		LogError(std::string(kLogTag) + " hooking '" + name + "' was rejected");
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " hooking '" + name + "' failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " hooking '" + name + "' failed");
	}
	return false;
}

void UnhookOne(const char* name, s2sdk::EventCallback callback, bool& hooked) {
	if (!hooked) {
		return;
	}
	hooked = false;
	if (__s2sdk_UnhookEvent == nullptr) {
		return;
	}
	try {
		s2sdk::UnhookEvent(plg::string(name), callback, s2sdk::HookMode::Pre);
	} catch (...) {
	}
}

void RegisterEventHooks() {
	for (EventRegistration& event : g_events) {
		event.hooked = false;
	}
	// HookEvent without UnhookEvent would leave the callback pointing into this library after the
	// unload (A7), so the pair is required before the first hook is installed.
	if (__s2sdk_HookEvent == nullptr || __s2sdk_UnhookEvent == nullptr) {
		LogError(std::string(kLogTag) +
				 " the s2sdk event bindings are unavailable (HookEvent/UnhookEvent pair required) - no event "
				 "is hooked");
		return;
	}
	for (EventRegistration& event : g_events) {
		event.hooked = HookOne(event.name, event.callback);
	}
}

void UnregisterEventHooks() {
	for (EventRegistration& event : g_events) {
		UnhookOne(event.name, event.callback, event.hooked);
	}
}

// --- map listeners (PITFALLS B1/B2) --------------------------------------------------------------

// s2sdk listener callbacks are plain function pointers called on the game thread: nothing here may
// throw, and no client call is made - only state changes.

void OnMapStartListener() {
	try {
		// From here on the clients and entities of the new map are in place.
		g_mapGate.OnMapStart();
		// OnMapEnd already released every menu of the previous map; this is a defensive path for a
		// plugin whose OnMapEnd was never delivered. The gate is open here, so a still-displayed
		// menu can be destroyed through the backend (a legal client call).
		ReleaseAllMenusWithLiveMap();
	} catch (...) {
		// An s2sdk listener must not throw into the engine.
	}
}

void OnMapEndListener() {
	try {
		// PITFALLS B2: from here until the next map start every s2sdk client query can crash. The
		// menu teardown below touches no client: CancelClientMenu(Disconnect) skips the backend's
		// close callback, so nothing reaches the client layer while it is being torn down, and the
		// DestroyMenu that follows has no viewer left.
		g_mapGate.OnMapEnd();
		ReleaseAllMenusForMapTeardown();
		g_reconcileAccumulator = 0.0;
	} catch (...) {
		// An s2sdk listener must not throw into the engine.
	}
}

void UnregisterMapListeners();

void RegisterMapListeners() {
	if (g_mapGate.ListenersRegistered()) {
		return;
	}
	// Both registrations *and* both unregistrations are required: a listener without its
	// unregister counterpart would keep pointing into this library after the unload (A7).
	if (__s2sdk_OnMapStart_Register == nullptr || __s2sdk_OnMapEnd_Register == nullptr ||
		__s2sdk_OnMapStart_Unregister == nullptr || __s2sdk_OnMapEnd_Unregister == nullptr) {
		// Without the map listeners the gate can never be opened safely (B1): every client call
		// stays refused and the plugin only logs.
		LogError(std::string(kLogTag) +
				 " the s2sdk map listeners are unavailable (OnMapStart/OnMapEnd Register+Unregister pair "
				 "required) - every client call stays refused");
		return;
	}
	// Set the bookkeeping before the calls: if the second registration throws after the first one
	// succeeded, UnregisterMapListeners() still has to take the first one back (PITFALLS A7).
	g_mapGate.SetListenersRegistered(true);
	try {
		s2sdk::OnMapStart_Register(&OnMapStartListener);
		s2sdk::OnMapEnd_Register(&OnMapEndListener);
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " registering the s2sdk map listeners failed: " + ex.what());
		UnregisterMapListeners();
	} catch (...) {
		LogError(std::string(kLogTag) + " registering the s2sdk map listeners failed");
		UnregisterMapListeners();
	}
}

void UnregisterMapListeners() {
	if (!g_mapGate.ListenersRegistered()) {
		return;
	}
	g_mapGate.SetListenersRegistered(false);
	if (__s2sdk_OnMapStart_Unregister != nullptr) {
		try {
			s2sdk::OnMapStart_Unregister(&OnMapStartListener);
		} catch (...) {
		}
	}
	if (__s2sdk_OnMapEnd_Unregister != nullptr) {
		try {
			s2sdk::OnMapEnd_Unregister(&OnMapEndListener);
		} catch (...) {
		}
	}
}

// --- permissions listener -----------------------------------------------------------------------

void OnPermissionsUserLoadedTrampoline(int64_t pluginID, uint64_t targetId, permissions::PlayerState state) {
	(void)pluginID;
	(void)state;
	try {
		OnUserLoaded(targetId);
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " exception in OnUserLoaded: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " unknown exception in OnUserLoaded");
	}
}

// The HEAD port's condition: the cookie store and both listener entry points must exist, otherwise
// the per-player settings are disabled as a whole (one warning, the rest keeps working).
void RegisterPermissionsListener() {
	g_permissionsAvailable = __permissions_GetCookie != nullptr && __permissions_SetCookie != nullptr &&
							 __permissions_OnUserLoaded_Register != nullptr &&
							 __permissions_OnUserLoaded_Unregister != nullptr;
	if (!g_permissionsAvailable) {
		LogWarn(std::string(kLogTag) + " permissions bindings are not available - the per-player "
									   "round-end settings cannot be loaded or saved");
		return;
	}
	try {
		const permissions::Status status = permissions::OnUserLoaded_Register(&OnPermissionsUserLoadedTrampoline);
		if (status == permissions::Status::Success) {
			g_permissionsListenerRegistered = true;
		} else {
			LogWarn(std::string(kLogTag) + " registering the permissions OnUserLoaded listener failed (status " +
					std::to_string(static_cast<int32_t>(status)) + ") - settings load from the reconcile instead");
		}
	} catch (...) {
		LogWarn(std::string(kLogTag) + " registering the permissions OnUserLoaded listener failed");
	}
}

void UnregisterPermissionsListener() {
	if (!g_permissionsListenerRegistered) {
		return;
	}
	g_permissionsListenerRegistered = false;
	if (__permissions_OnUserLoaded_Unregister == nullptr) {
		return;
	}
	try {
		permissions::OnUserLoaded_Unregister(&OnPermissionsUserLoadedTrampoline);
	} catch (...) {
	}
}

// --- plugin lifecycle ----------------------------------------------------------------------------

class RoundEndThingsPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;
};

RoundEndThingsPlugin g_roundEndThingsPlugin;

// A reload starts from a clean sheet; OnPluginEnd already unregistered everything, but a host that
// calls Start twice must not leak a hook/listener/menu either.
void ResetState() {
	g_settings = roundend::Settings{};
	g_players.fill(roundend::PlayerSettings{});
	g_slots.fill(SlotState{});
	g_menus.fill(SlotMenu{});
	g_destroyQueue.clear();
	g_permissionsAvailable = false;
	g_permissionsListenerRegistered = false;
	g_reconcileAccumulator = 0.0;
	g_reservedMenuIdZero = false;
	g_startupConfigReloaded = false;
	g_velocityOffset = -1;
	g_gravityOffset = -1;
	g_actualGravityOffset = -1;
	g_loggedSchemaFailure = false;
	g_loggedMenuUnavailable = false;
	g_loggedMissingIdentity = false;
	g_tearingDown = false;
	g_mapGate.Reset();
}

plg::PluginResult RoundEndThingsPlugin::OnPluginStart() {
	try {
		// Defensive re-entry cleanup: no-ops when nothing is registered.
		g_tearingDown = false;
		UnregisterEventHooks();
		RemoveCommands();
		UnregisterPermissionsListener();
		UnregisterMapListeners();
		if (g_mapGate.Ready()) {
			DestroyAllMenusBestEffort(); // a Start without End left a menu set behind on a live map
		} else {
			// Before the first map s2sdk's menu manager may still dereference a null gpGlobals; a
			// menu can only exist on a running map, so there is nothing to release here - only the
			// reserved id 0, whose destruction touches no client.
			for (SlotMenu& menu : g_menus) {
				ClearSlotMenuState(menu);
			}
			g_destroyQueue.clear();
			ReleaseReservedMenuId();
		}
		ResetState();

		// File I/O only: no client, entity or schema call happens in OnPluginStart (PITFALLS B1).
		LoadConfig();

		// Bindings first, then the registrations: all of them are safe before the first map.
		RegisterPermissionsListener();
		RegisterCommands();
		RegisterEventHooks();
		RegisterMapListeners();
		// Reserve s2sdk's unusable MenuId 0 before any menu can be created (PITFALLS C10).
		BurnReservedMenuId();

		LogInfo(std::string(kLogTag) + " ready");
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " OnPluginStart failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " OnPluginStart failed with an unknown exception");
	}
	return {}; // never Failed: that stops every plugify plugin (PITFALLS A1)
}

plg::PluginResult RoundEndThingsPlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	try {
		// PITFALLS B1: a plugin loaded into a running map never saw OnMapStart; its first update is
		// the "a map is up" signal. An OnMapEnd seen since OnPluginStart means the server is
		// between maps, so the latch waits for the next OnMapStart instead.
		g_mapGate.OnPluginUpdate();

		// The original's StartupServer re-read settings.ini once the server was up.
		if (!g_startupConfigReloaded && g_mapGate.Ready()) {
			g_startupConfigReloaded = true;
			LoadConfig();
		}

		// Selections recorded by the menu handler: applied here, outside the menu manager's lock,
		// after the owner identity has been re-checked (PITFALLS C10 + slot reuse).
		ApplyPendingChoices();
		// Deferred menu destruction / re-open: outside the menu manager lock (PITFALLS C10).
		ProcessMenuWork();

		// Reconcile cadence (header): accumulate the host's delta; no s2sdk timer is involved.
		if (!g_mapGate.Ready()) {
			g_reconcileAccumulator = 0.0;
			return {};
		}
		const double seconds = std::chrono::duration<double>(dt).count();
		if (!(seconds > 0.0) || !std::isfinite(seconds)) {
			return {};
		}
		g_reconcileAccumulator += seconds;
		if (g_reconcileAccumulator < kReconcileIntervalSeconds) {
			return {};
		}
		// At most one reconcile per update. A long delta (server hitch, host throttling) drops the
		// whole missed intervals instead of replaying them: no unbounded catch-up.
		g_reconcileAccumulator = std::fmod(g_reconcileAccumulator, kReconcileIntervalSeconds);
		ReconcileSlots();
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " update failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " update failed with an unknown exception");
	}
	return {};
}

plg::PluginResult RoundEndThingsPlugin::OnPluginEnd() {
	try {
		// PITFALLS A7: every registration of this library is removed again, so `plg reload` leaves
		// nothing that points into the unloaded .so. There is no s2sdk timer to kill (the reconcile
		// lives in OnPluginUpdate).
		g_tearingDown = true;
		UnregisterEventHooks();
		RemoveCommands();
		UnregisterPermissionsListener();
		UnregisterMapListeners();
		// Frees every menu instance (Disconnect teardown between maps) and the reserved id 0, so no
		// callback can point into the library after it is unloaded.
		DestroyAllMenusBestEffort();

		// The gate closes after the menu teardown: a callback the engine may still hold for a
		// moment must not reach a client, and nothing may call into this library after it is gone.
		g_mapGate.Close();
		ResetState();
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " OnPluginEnd failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " OnPluginEnd failed with an unknown exception");
	}
	return {};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_roundEndThingsPlugin)
