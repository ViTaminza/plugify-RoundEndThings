// roundend_plugin_tests — the plugin-level test of the pure-s2sdk RoundEndThings port
// (docs/PLUGIN_STANDARD.md §7: no pisexcompat, no guard library; the optional hudmenu *API* is kept
// for the player's own "!menu" type choice, which is real functionality, not a wrapper library).
//
// The plugin TU is compiled into this test (`#include "../src/plugin.cpp"`), so the whole plugin
// code path is exercised; every __s2sdk_* / __permissions_* binding slot is a recording fake, the
// Plugify host pointers are fakes and the settings.ini comes from a temporary <configs> root.
// Covered:
//
//   1. startup (A1): OnPluginStart answers Ok, every registration happens exactly once (two Pre
//      event hooks, the map listeners, both commands, the permissions OnUserLoaded listener), the
//      production-format settings.ini is read, and no client/entity/schema call is made before the
//      first map (B1);
//   2. the map gate (B1/B2): round_end/commands are inert before OnMapStart and between OnMapEnd
//      and the next OnMapStart, the first OnPluginUpdate latches readiness for a plugin loaded into
//      a running map, an OnMapEnd seen before that latch keeps the gate shut, and a missing map
//      listener binding disables every client call instead of crashing;
//   3. round_end/round_start: the exact convar commands of the original and the raw
//      m_flVelocityModifier/m_flGravityScale/m_flActualGravityScale writes - alive slots only,
//      bots included, SourceTV/dead/no-controller/no-pawn slots skipped, a valid negative
//      CEntityHandle accepted while 0/-1 are refused, size=4, changeState=false, chainOffset=-2,
//      offsets resolved once and cached (C6/C8), the per-player opt-outs honoured and round_start
//      restoring 1.0;
//   4. the config: the pisexcompat candidate order with the raw cwd file first, the first existing
//      file authoritative even when malformed, missing/malformed files keeping the original's
//      defaults, and the original's StartupServer re-read happening once on the first update with
//      an open gate;
//   5. the commands (C11): "mm_ret" answers the Console context only and "ret" the Chat context
//      only (a chat "!mm_ret" and a console "ret" stay untouched), the server console opens no
//      menu, the menu content follows the enabled features, and the ownership split uses
//      RemoveCommand for the names this plugin created and RemoveCommandListener for a
//      pre-existing name;
//   6. the menu (C10): the original's item labels, s2sdk's unusable MenuId 0 burned at start (first
//      real menu id >= 1; the fake models the manager's 0 == "no menu" sentinel), the handler only
//      recording the selection (no toggle/print/cookie inside the menu-manager lock), the update
//      applying it after re-checking SteamID64 + controller serial (a reused/disconnected slot gets
//      nothing), DestroyMenu deferred outside the handler and the re-open carrying the "[X]" label;
//      the map-end/unload teardown ends a live display with CancelClientMenu(Disconnect) first and
//      verifies it is gone before DestroyMenu (the fake counts backend close calls: 0 between
//      maps); when Cancel answers false while the display is still active (an s2sdk contract
//      violation) the menu is switched to the builtin no-op-close "chat"/"console" type and
//      verified before the destroy - still 0 backend calls - and when even that switch fails the
//      unload reports A7_UNRESOLVED instead of hiding the menu; the reserved dummy is released on
//      unload, and a partial binding set (including SetMenuType/GetMenuType) creates no menu;
//      the menu type follows the player's hudmenu "!menu" choice (chat/centerhtml/button/hud/wasd,
//      falling back to "button" for a down backend) and the default HUD_LAYOUT mapping;
//      the owner identity is required before any menu is created (nonzero SteamID64 + valid nonzero
//      controller handle; missing accessors/invalid 0/-1 -> no menu, a valid negative handle is
//      accepted) and applied/selections are compared exactly (same SteamID64 with a new controller
//      serial -> no toggle/chat/cookie);
//   7. permissions (the cookie layer of the HEAD port): the once-per-second update-driven reconcile
//      (no s2sdk timer, no reentrant timer mutex), every cookie shape (int64/string/bool/...), the
//      unusable-value warning, CookieNotFound vs TargetUserNotFound retries, the
//      pluginID/steamid/name/value of SetCookie, the retry of a failed save, the OnUserLoaded
//      listener, slot reuse resetting the settings and dropping a stale save, and absent bindings
//      disabling only the cookie feature;
//   8. absent bindings: every missing s2sdk/permissions accessor logs and skips only its feature,
//      OnPluginStart/OnPluginEnd stay Ok and no null call happens;
//   9. reload (A7): OnPluginEnd removes both hooks with the same callbacks/mode, both map listeners,
//      both commands (by ownership), the permissions listener and every menu, closes the gate and
//      drops the schema cache; a stale callback fired afterwards touches no client; starting again
//      registers everything exactly once; every registration requires its complete teardown pair
//      beforehand (a missing RemoveCommand/RemoveCommandListener/UnhookEvent or any of the four map
//      listener endpoints registers nothing and leaves no dangling callback);
//  10. the player-typed event accessors: round_end/round_start carry no player field, so
//      GetEventPlayerSlot/GetEventInt are never called (the test installs counting fakes and
//      asserts zero) and the events work with those bindings absent.
//
// No pisexcompat, no Plugify host, no engine. Run by ../../test.sh (build/tests/roundend_plugin_tests).

#include "../src/plugin.cpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

#ifndef ROUNDEND_TEST_DATA_DIR
#define ROUNDEND_TEST_DATA_DIR ""
#endif

namespace test {

// ---------------------------------------------------------------------------------------------
// checks
// ---------------------------------------------------------------------------------------------

int g_checks = 0;
int g_failures = 0;

void Section(const char* name) {
	std::printf("== %s\n", name);
}

void Check(bool condition, const std::string& what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

void CheckEqInt(long long actual, long long expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %lld, got %lld)\n", what.c_str(), expected, actual);
	}
}

void CheckEqStr(std::string_view actual, std::string_view expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected '%s', got '%s')\n", what.c_str(), std::string(expected).c_str(),
					std::string(actual).c_str());
	}
}

void CheckNear(float actual, float expected, const std::string& what) {
	++g_checks;
	if (std::fabs(actual - expected) > 1e-4f) {
		++g_failures;
		std::printf("FAIL: %s (expected %g, got %g)\n", what.c_str(), expected, actual);
	}
}

std::string ToStd(const plg::string& text) {
	return std::string(text.data(), text.size());
}

// ---------------------------------------------------------------------------------------------
// what the fakes answer
// ---------------------------------------------------------------------------------------------

constexpr int kSlots = 64;
constexpr int32_t kAutoHandle = 0x7fffffff;

struct FakeSlot {
	bool connected = false;
	bool in_game = false;
	bool source_tv = false;
	bool fake = false;
	bool controller = false;
	bool alive = false;
	bool pawn = false;
	uint64_t steam = 0;
	// Bumped every AddPlayer()/AddBot(): the controller entity of a reused slot is a new entity
	// with a new serial, so its CEntityHandle differs (the menu identity check relies on it).
	uint32_t generation = 0;
	// kAutoHandle = "derive a handle from the slot"; any other value is answered as is.
	int32_t handle = kAutoHandle;
	// kAutoHandle = "derive the controller handle from slot + generation"; any other value (0, -1,
	// a valid negative serial handle, ...) is answered as is.
	int32_t controller_handle = kAutoHandle;
};

struct FakeState {
	std::array<FakeSlot, kSlots> slots{};
	bool valid_pointer = true;
	std::map<int32_t, bool> handle_validity; // missing = valid
	std::map<std::string, permissions::Status> get_cookie_status_by_name;
	std::map<std::string, permissions::Status> set_cookie_status_by_name;
	int set_cookie_failures_remaining = 0;
	bool throw_on_get_cookie = false;
};

FakeState g_fake;

std::string g_configs_dir;
std::string g_data_dir;

// Recording state shared by the fakes.
std::vector<std::string> g_errors;
std::vector<std::string> g_warnings;
bool AnyErrorContains(std::string_view needle) {
	for (const std::string& error : g_errors) {
		if (error.find(needle) != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool AnyWarningContains(std::string_view needle) {
	for (const std::string& warning : g_warnings) {
		if (warning.find(needle) != std::string::npos) {
			return true;
		}
	}
	return false;
}

std::vector<int> g_client_calls;
std::vector<int> g_controller_calls;
std::vector<int> g_handle_calls;
std::vector<std::string> g_schema_lookups;
std::map<std::string, int32_t> g_schema_offsets;

struct FloatWrite {
	int32_t handle = 0;
	int32_t offset = 0;
	float value = 0.0f;
	int32_t size = 0;
	bool change_state = true;
	int32_t chain_offset = 0;
};

std::vector<FloatWrite> g_float_writes;
std::vector<std::string> g_server_commands;

struct PrintedLine {
	int slot = -1;
	std::string message;
};

std::vector<PrintedLine> g_printed;

// Events and map listeners.
struct HookRecord {
	std::string name;
	s2sdk::EventCallback callback = nullptr;
	s2sdk::HookMode mode = s2sdk::HookMode::Pre;
};

std::vector<HookRecord> g_hooks;
std::vector<HookRecord> g_hook_calls;
std::vector<HookRecord> g_unhook_calls;

s2sdk::OnMapStartCallback g_on_map_start = nullptr;
s2sdk::OnMapEndCallback g_on_map_end = nullptr;
bool g_map_start_registered = false;
bool g_map_end_registered = false;
bool g_map_start_unregistered = false;
bool g_map_end_unregistered = false;
bool g_throw_on_map_end_register = false;

// Commands.
struct CommandRecord {
	std::string name;
	s2sdk::ConCommandCallback callback = nullptr;
	s2sdk::ConVarFlag flags = s2sdk::ConVarFlag::None;
	s2sdk::HookMode mode = s2sdk::HookMode::Pre;
	bool created = false;
};

std::vector<CommandRecord> g_command_calls;
std::vector<CommandRecord> g_remove_command_calls;
std::vector<CommandRecord> g_remove_listener_calls;
std::set<std::string> g_preexisting_commands;

// Player-typed event accessors: installed as counting fakes, but the plugin must never need them.
int g_event_player_slot_calls = 0;
int g_event_int_calls = 0;

// Permissions.
std::map<std::string, plg::any> g_cookies; // "steamid:name" -> value
std::vector<std::string> g_get_cookie_calls;
struct SetCookieRecord {
	int64_t plugin_id = 0;
	uint64_t target = 0;
	std::string name;
	plg::any value;
	bool dont_broadcast = true;
};
std::vector<SetCookieRecord> g_set_cookie_calls;
permissions::UserLoadedCallback g_user_loaded = nullptr;
bool g_user_loaded_registered = false;
bool g_user_loaded_unregistered = false;
permissions::Status g_user_loaded_register_status = permissions::Status::Success;
int64_t g_plugin_id = 42;

// Menus.
struct FakeMenu {
	std::string title;
	std::string type;
	s2sdk::MenuHandlerCallback handler = nullptr;
	std::vector<std::pair<std::string, std::string>> items;
	bool exit_button = false;
	bool close_on_select = true;
};

std::map<s2sdk::MenuId, FakeMenu> g_menus;
s2sdk::MenuId g_next_menu_id = 0; // like s2sdk, the first menu id is 0
std::array<std::optional<s2sdk::MenuId>, kSlots> g_client_menu{};
std::set<std::string> g_menu_types{"chat", "console", "centerhtml", "button"};
std::vector<std::string> g_menu_calls;
std::vector<s2sdk::MenuId> g_destroyed_menus;
bool g_in_handler = false;
int g_destroy_calls_in_handler = 0;
// The backend's close callback (hudmenu's HideForSlot / PrintHtmlCentre / ...) is a client call:
// the fake counts the ones the plugin's cleanup provokes, so a test can assert that the map-down
// teardown makes none of them.
int g_backend_close_calls = 0;
// Contract-violation knobs for the map-down fail-safe: CancelClientMenu answers false while the
// display stays active, and/or SetMenuType refuses the no-op-close switch.
bool g_cancel_fails = false;
bool g_set_menu_type_fails = false;
std::vector<std::string> g_type_switches;

// The optional hudmenu API answer per slot (the player's own "!menu" choice).
std::array<std::string, kSlots> g_hudmenu_choice{};
int g_hudmenu_calls = 0;

// Timers: s2sdk's timer system is deliberately never used by this plugin.
int g_timer_calls = 0;

bool g_plugin_started = false;

// ---------------------------------------------------------------------------------------------
// helpers over the recorded state
// ---------------------------------------------------------------------------------------------

const FloatWrite* FindFloatWrite(int32_t handle, int32_t offset) {
	for (const FloatWrite& write : g_float_writes) {
		if (write.handle == handle && write.offset == offset) {
			return &write;
		}
	}
	return nullptr;
}

int CountFloatWrites(int32_t handle) {
	int count = 0;
	for (const FloatWrite& write : g_float_writes) {
		if (write.handle == handle) {
			++count;
		}
	}
	return count;
}

const HookRecord* FindHook(const char* name) {
	for (const HookRecord& hook : g_hook_calls) {
		if (hook.name == name) {
			return &hook;
		}
	}
	return nullptr;
}

const CommandRecord* FindCommand(const char* name) {
	for (const CommandRecord& command : g_command_calls) {
		if (command.name == name) {
			return &command;
		}
	}
	return nullptr;
}

// s2sdk's menu manager stores the open menu in the same field as "no menu" (0), so a display with
// MenuId 0 is indistinguishable from nobody having a menu open (menu_manager.cpp:507/538/770).
// The fake models that sentinel: only a non-zero id counts as a displayed menu.
bool HasClientMenu(int slot) {
	return g_client_menu[static_cast<std::size_t>(slot)].has_value() &&
		   *g_client_menu[static_cast<std::size_t>(slot)] != 0;
}

int DisplayedMenu(int slot) {
	return HasClientMenu(slot) ? 1 : 0;
}

const FakeMenu* DisplayedMenuOf(int slot) {
	if (!HasClientMenu(slot)) {
		return nullptr;
	}
	const auto it = g_menus.find(*g_client_menu[static_cast<std::size_t>(slot)]);
	return it == g_menus.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------------------------
// temporary directories
// ---------------------------------------------------------------------------------------------

std::vector<std::filesystem::path> g_temp_dirs;

std::filesystem::path NewTempDir() {
	static int counter = 0;
	const std::filesystem::path dir =
			std::filesystem::temp_directory_path() /
			("roundend_plugin_tests_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
	std::filesystem::create_directories(dir);
	g_temp_dirs.push_back(dir);
	return dir;
}

void WriteTextFile(const std::string& path, std::string_view content) {
	const std::filesystem::path file(path);
	std::filesystem::create_directories(file.parent_path());
	std::ofstream stream(file, std::ios::binary | std::ios::trunc);
	stream << content;
}

void CleanupTempDirs() {
	for (const std::filesystem::path& dir : g_temp_dirs) {
		std::error_code error;
		std::filesystem::remove_all(dir, error);
	}
	g_temp_dirs.clear();
}

class ScopedCwd {
public:
	explicit ScopedCwd(const std::filesystem::path& dir) : m_previous(std::filesystem::current_path()) {
		std::filesystem::current_path(dir);
	}
	~ScopedCwd() {
		std::error_code error;
		std::filesystem::current_path(m_previous, error);
	}
	ScopedCwd(const ScopedCwd&) = delete;
	ScopedCwd& operator=(const ScopedCwd&) = delete;

private:
	std::filesystem::path m_previous;
};

void WriteConfig(std::string_view content) {
	WriteTextFile(g_configs_dir + "/RoundEndThings/settings.ini", content);
}

// All features on, with visible boost values for the round-end writes.
constexpr const char* kEnabledConfig =
		"\"Config\"\n"
		"{\n"
		"    \"bhop_enabled\" \"1\"\n"
		"    \"speed_enabled\" \"1\"\n"
		"    \"gravity_enabled\" \"1\"\n"
		"    \"speed\" \"2.0\"\n"
		"    \"gravity\" \"0.5\"\n"
		"}\n";

// ---------------------------------------------------------------------------------------------
// fake s2sdk bindings
// ---------------------------------------------------------------------------------------------

void* ControllerPointer(int slot) {
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x100000) +
								   static_cast<std::uintptr_t>(slot) * 0x20);
}

void* PawnPointer(int slot) {
	return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x200000) +
								   static_cast<std::uintptr_t>(slot) * 0x20);
}

int SlotFromPointer(void* pointer) {
	const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(pointer);
	const std::uintptr_t base = value >= 0x200000 ? 0x200000 : 0x100000;
	if (value < base) {
		return -1;
	}
	const std::uintptr_t offset = value - base;
	if (offset % 0x20 != 0) {
		return -1;
	}
	const std::uintptr_t slot = offset / 0x20;
	return slot < static_cast<std::uintptr_t>(kSlots) ? static_cast<int>(slot) : -1;
}

void* FakePlayerSlotToEntPointer(int32_t slot) {
	g_controller_calls.push_back(static_cast<int>(slot));
	if (slot < 0 || slot >= kSlots || !g_fake.slots[static_cast<std::size_t>(slot)].controller) {
		return nullptr;
	}
	return ControllerPointer(static_cast<int>(slot));
}

int32_t FakeEntPointerToEntIndex(void* entity) {
	const int slot = SlotFromPointer(entity);
	// Player controllers are real entities: the world is index 0, a controller never is.
	return slot >= 0 ? static_cast<int32_t>(slot) + 1 : 0;
}

int32_t ControllerHandleValue(int slot) {
	const FakeSlot& fake = g_fake.slots[static_cast<std::size_t>(slot)];
	if (fake.controller_handle != kAutoHandle) {
		return fake.controller_handle;
	}
	return static_cast<int32_t>(5000 + slot * 16 + fake.generation);
}

int32_t FakePlayerSlotToEntHandle(int32_t slot) {
	g_handle_calls.push_back(static_cast<int>(slot));
	if (slot < 0 || slot >= kSlots) {
		return -1;
	}
	return ControllerHandleValue(static_cast<int>(slot));
}

int32_t FakeEntPointerToEntHandle(void* entity) {
	const int slot = SlotFromPointer(entity);
	if (slot < 0) {
		return -1;
	}
	const int32_t configured = g_fake.slots[static_cast<std::size_t>(slot)].handle;
	return configured == kAutoHandle ? static_cast<int32_t>(2000 + slot) : configured;
}

bool FakeIsValidEntPointer(void* /*entity*/) {
	return g_fake.valid_pointer;
}

bool FakeIsValidEntHandle(int32_t handle) {
	const auto it = g_fake.handle_validity.find(handle);
	return it == g_fake.handle_validity.end() ? true : it->second;
}

bool FakeIsClientConnected(int32_t slot) {
	g_client_calls.push_back(static_cast<int>(slot));
	return slot >= 0 && slot < kSlots && g_fake.slots[static_cast<std::size_t>(slot)].connected;
}

bool FakeIsClientInGame(int32_t slot) {
	g_client_calls.push_back(static_cast<int>(slot));
	return slot >= 0 && slot < kSlots && g_fake.slots[static_cast<std::size_t>(slot)].in_game;
}

bool FakeIsClientSourceTV(int32_t slot) {
	g_client_calls.push_back(static_cast<int>(slot));
	return slot >= 0 && slot < kSlots && g_fake.slots[static_cast<std::size_t>(slot)].source_tv;
}

bool FakeIsFakeClient(int32_t slot) {
	g_client_calls.push_back(static_cast<int>(slot));
	return slot >= 0 && slot < kSlots && g_fake.slots[static_cast<std::size_t>(slot)].fake;
}

bool FakeIsClientAlive(int32_t slot) {
	g_client_calls.push_back(static_cast<int>(slot));
	if (slot < 0 || slot >= kSlots) {
		return false;
	}
	const FakeSlot& fake = g_fake.slots[static_cast<std::size_t>(slot)];
	return fake.connected && fake.in_game && fake.alive;
}

uint64_t FakeGetClientSteamID64(int32_t slot) {
	g_client_calls.push_back(static_cast<int>(slot));
	if (slot < 0 || slot >= kSlots) {
		return 0;
	}
	return g_fake.slots[static_cast<std::size_t>(slot)].steam;
}

void* FakeGetClientPawn(int32_t slot) {
	g_client_calls.push_back(static_cast<int>(slot));
	if (slot < 0 || slot >= kSlots || !g_fake.slots[static_cast<std::size_t>(slot)].pawn) {
		return nullptr;
	}
	return PawnPointer(static_cast<int>(slot));
}

int32_t FakeGetSchemaOffset(const plg::string& className, const plg::string& memberName) {
	const std::string key = ToStd(className) + "::" + ToStd(memberName);
	g_schema_lookups.push_back(key);
	const auto it = g_schema_offsets.find(key);
	return it == g_schema_offsets.end() ? -1 : it->second;
}

void FakeSetEntDataFloat(int32_t entityHandle, int32_t offset, double value, int32_t size, bool changeState,
						 int32_t chainOffset) {
	g_float_writes.push_back(FloatWrite{entityHandle, offset, static_cast<float>(value), size, changeState,
										chainOffset});
}

void FakeServerCommand(const plg::string& command) {
	g_server_commands.push_back(ToStd(command));
}

void FakePrintToChat(int32_t slot, const plg::string& message) {
	g_printed.push_back(PrintedLine{static_cast<int>(slot), ToStd(message)});
}

int32_t FakeGetEventPlayerSlot(void* /*event*/, const plg::string& /*key*/) {
	++g_event_player_slot_calls;
	return 0;
}

int32_t FakeGetEventInt(void* /*event*/, const plg::string& /*key*/) {
	++g_event_int_calls;
	return 0;
}

s2sdk::EventHookError FakeHookEvent(const plg::string& name, s2sdk::EventCallback callback, s2sdk::HookMode mode) {
	const HookRecord record{ToStd(name), callback, mode};
	g_hook_calls.push_back(record);
	g_hooks.push_back(record);
	return s2sdk::EventHookError::Okay;
}

s2sdk::EventHookError FakeUnhookEvent(const plg::string& name, s2sdk::EventCallback callback, s2sdk::HookMode mode) {
	const HookRecord record{ToStd(name), callback, mode};
	g_unhook_calls.push_back(record);
	for (auto it = g_hooks.begin(); it != g_hooks.end(); ++it) {
		if (it->name == record.name && it->callback == record.callback && it->mode == record.mode) {
			g_hooks.erase(it);
			break;
		}
	}
	return s2sdk::EventHookError::Okay;
}

void FakeOnMapStartRegister(s2sdk::OnMapStartCallback callback) {
	g_on_map_start = callback;
	g_map_start_registered = callback != nullptr;
}

void FakeOnMapEndRegister(s2sdk::OnMapEndCallback callback) {
	if (g_throw_on_map_end_register) {
		throw std::runtime_error("fake OnMapEnd_Register failure");
	}
	g_on_map_end = callback;
	g_map_end_registered = callback != nullptr;
}

void FakeOnMapStartUnregister(s2sdk::OnMapStartCallback /*callback*/) {
	g_map_start_unregistered = true;
	g_map_start_registered = false;
	g_on_map_start = nullptr;
}

void FakeOnMapEndUnregister(s2sdk::OnMapEndCallback /*callback*/) {
	g_map_end_unregistered = true;
	g_map_end_registered = false;
	g_on_map_end = nullptr;
}

bool FakeAddConsoleCommand(const plg::string& name, const plg::string& /*description*/, s2sdk::ConVarFlag flags,
						   s2sdk::ConCommandCallback callback, s2sdk::HookMode mode) {
	const std::string command = ToStd(name);
	const CommandRecord record{command, callback, flags, mode, g_preexisting_commands.count(command) == 0};
	g_command_calls.push_back(record);
	return record.created;
}

bool FakeRemoveCommand(const plg::string& name, s2sdk::ConCommandCallback callback) {
	g_remove_command_calls.push_back(CommandRecord{ToStd(name), callback});
	return true;
}

bool FakeRemoveCommandListener(const plg::string& name, s2sdk::ConCommandCallback callback, s2sdk::HookMode mode) {
	g_remove_listener_calls.push_back(CommandRecord{ToStd(name), callback, s2sdk::ConVarFlag::None, mode});
	return true;
}

s2sdk::TimerId FakeCreateTimer(double /*delay*/, s2sdk::TimerCallback /*callback*/, s2sdk::TimerFlag /*flags*/,
							   const plg::vector<plg::any>& /*userData*/) {
	++g_timer_calls;
	return 0;
}

void FakeKillsTimer(s2sdk::TimerId /*timer*/) {
	++g_timer_calls;
}

void FakeRescheduleTimer(s2sdk::TimerId /*timer*/, double /*newDelay*/) {
	++g_timer_calls;
}

// -- permissions -------------------------------------------------------------------------------

permissions::Status FakeGetCookie(uint64_t targetID, const plg::string& name, plg::any& value) {
	const std::string cookie = ToStd(name);
	g_get_cookie_calls.push_back(std::to_string(targetID) + ":" + cookie);
	if (g_fake.throw_on_get_cookie) {
		throw std::runtime_error("fake GetCookie failure");
	}
	const auto status = g_fake.get_cookie_status_by_name.find(cookie);
	if (status != g_fake.get_cookie_status_by_name.end() && status->second != permissions::Status::Success) {
		return status->second;
	}
	const auto it = g_cookies.find(std::to_string(targetID) + ":" + cookie);
	if (it == g_cookies.end()) {
		return permissions::Status::CookieNotFound;
	}
	value = it->second;
	return permissions::Status::Success;
}

permissions::Status FakeSetCookie(int64_t pluginID, uint64_t targetID, const plg::string& name,
								  const plg::any& cookie, bool dontBroadcast) {
	const std::string cookie_name = ToStd(name);
	g_set_cookie_calls.push_back(SetCookieRecord{pluginID, targetID, cookie_name, cookie, dontBroadcast});
	const auto status = g_fake.set_cookie_status_by_name.find(cookie_name);
	if (status != g_fake.set_cookie_status_by_name.end() && status->second != permissions::Status::Success) {
		return status->second;
	}
	if (g_fake.set_cookie_failures_remaining > 0) {
		--g_fake.set_cookie_failures_remaining;
		return permissions::Status::TargetUserNotFound;
	}
	g_cookies[std::to_string(targetID) + ":" + cookie_name] = cookie;
	return permissions::Status::Success;
}

permissions::Status FakeOnUserLoadedRegister(permissions::UserLoadedCallback callback) {
	if (g_user_loaded_register_status != permissions::Status::Success) {
		return g_user_loaded_register_status;
	}
	g_user_loaded = callback;
	g_user_loaded_registered = callback != nullptr;
	return permissions::Status::Success;
}

permissions::Status FakeOnUserLoadedUnregister(permissions::UserLoadedCallback /*callback*/) {
	g_user_loaded_unregistered = true;
	g_user_loaded_registered = false;
	g_user_loaded = nullptr;
	return permissions::Status::Success;
}

// -- menus -------------------------------------------------------------------------------------

void Dispatch(s2sdk::MenuId id, s2sdk::MenuAction action, int32_t slot, int32_t param);

s2sdk::MenuId FakeCreateMenu(const plg::string& title, s2sdk::MenuHandlerCallback handler,
							 const plg::string& menuType) {
	const s2sdk::MenuId id = g_next_menu_id++;
	FakeMenu menu;
	menu.title = ToStd(title);
	menu.type = ToStd(menuType);
	menu.handler = handler;
	g_menus[id] = std::move(menu);
	g_menu_calls.push_back("create:" + std::to_string(id));
	return id;
}

bool FakeDestroyMenu(s2sdk::MenuId id) {
	g_menu_calls.push_back("destroy:" + std::to_string(id));
	if (g_in_handler) {
		++g_destroy_calls_in_handler;
	}
	const auto it = g_menus.find(id);
	if (it == g_menus.end()) {
		return false;
	}
	if (id != 0) { // id 0 is the manager's "no menu" value, so it can never have a viewer
		// DestroyMenu ends a live viewer with MenuCancelReason::Destroyed, which calls the current
		// menu type's close callback; the builtin "chat"/"console" closers are no-ops.
		const bool noOpClose = it->second.type == "chat" || it->second.type == "console";
		for (int slot = 0; slot < kSlots; ++slot) {
			if (HasClientMenu(slot) && *g_client_menu[static_cast<std::size_t>(slot)] == id) {
				g_client_menu[static_cast<std::size_t>(slot)].reset();
				if (!noOpClose) {
					++g_backend_close_calls;
				}
				Dispatch(id, s2sdk::MenuAction::Cancel, slot,
						 static_cast<int32_t>(s2sdk::MenuCancelReason::Destroyed));
				Dispatch(id, s2sdk::MenuAction::End, slot, 0);
			}
		}
	}
	g_menus.erase(it);
	g_destroyed_menus.push_back(id);
	return true;
}

int32_t FakeAddMenuItem(s2sdk::MenuId id, const plg::string& info, const plg::string& display,
						s2sdk::MenuItemStyle /*style*/) {
	const auto it = g_menus.find(id);
	if (it == g_menus.end()) {
		return -1;
	}
	it->second.items.emplace_back(ToStd(info), ToStd(display));
	g_menu_calls.push_back("item:" + ToStd(info));
	return static_cast<int32_t>(it->second.items.size()) - 1;
}

bool FakeSetMenuExitButton(s2sdk::MenuId id, bool enabled) {
	const auto it = g_menus.find(id);
	if (it == g_menus.end()) {
		return false;
	}
	it->second.exit_button = enabled;
	return true;
}

bool FakeSetMenuCloseOnSelect(s2sdk::MenuId id, bool enabled) {
	const auto it = g_menus.find(id);
	if (it == g_menus.end()) {
		return false;
	}
	it->second.close_on_select = enabled;
	return true;
}

bool FakeDisplayMenu(s2sdk::MenuId id, int32_t slot, int32_t /*time*/) {
	if (slot < 0 || slot >= kSlots) {
		return false;
	}
	const auto it = g_menus.find(id);
	if (it == g_menus.end()) {
		return false;
	}
	const std::string type = it->second.type.empty() ? std::string("button") : it->second.type;
	if (g_menu_types.count(type) == 0) {
		return false;
	}
	if (HasClientMenu(slot)) {
		const s2sdk::MenuId previous = *g_client_menu[static_cast<std::size_t>(slot)];
		g_client_menu[static_cast<std::size_t>(slot)].reset();
		Dispatch(previous, s2sdk::MenuAction::Cancel, slot,
				 static_cast<int32_t>(s2sdk::MenuCancelReason::Interrupted));
		Dispatch(previous, s2sdk::MenuAction::End, slot, 0);
	}
	// Displaying MenuId 0 "renders" in s2sdk, but the client state then holds the same 0 as
	// "no menu": HasClientMenu() is false from here on, exactly like the engine.
	g_client_menu[static_cast<std::size_t>(slot)] = id;
	Dispatch(id, s2sdk::MenuAction::Start, slot, 0);
	g_menu_calls.push_back("display:" + std::to_string(id) + ":" + std::to_string(slot));
	return true;
}

plg::string FakeGetMenuItemInfoText(s2sdk::MenuId id, int32_t index) {
	const auto it = g_menus.find(id);
	if (it == g_menus.end() || index < 0 || static_cast<std::size_t>(index) >= it->second.items.size()) {
		return {};
	}
	return plg::string(it->second.items[static_cast<std::size_t>(index)].first);
}

bool FakeIsMenuTypeRegistered(const plg::string& name) {
	return g_menu_types.count(ToStd(name)) != 0;
}

bool FakeSetMenuType(s2sdk::MenuId id, const plg::string& typeName) {
	const auto it = g_menus.find(id);
	if (it == g_menus.end() || g_set_menu_type_fails) {
		return false;
	}
	it->second.type = ToStd(typeName);
	g_type_switches.push_back(it->second.type);
	return true;
}

plg::string FakeGetMenuType(s2sdk::MenuId id) {
	const auto it = g_menus.find(id);
	return it == g_menus.end() ? plg::string() : plg::string(it->second.type);
}

bool FakeIsValidMenu(s2sdk::MenuId id) {
	return g_menus.count(id) != 0;
}

plg::string FakeHudmenuGetClientMenuType(int32_t slot) {
	++g_hudmenu_calls;
	if (slot < 0 || slot >= kSlots) {
		return {};
	}
	return plg::string(g_hudmenu_choice[static_cast<std::size_t>(slot)]);
}

// s2sdk's GetClientMenu returns 0 both for "no menu" and for the unusable MenuId 0.
s2sdk::MenuId FakeGetClientMenu(int32_t slot) {
	if (slot < 0 || slot >= kSlots || !HasClientMenu(slot)) {
		return 0;
	}
	return *g_client_menu[static_cast<std::size_t>(slot)];
}

// MenuManager::CancelClientMenu: EndClientMenu -> CloseClientDisplay(slot, reason != Disconnect);
// only a non-Disconnect reason reaches the menu type backend (a client call).
bool FakeCancelClientMenu(int32_t slot, s2sdk::MenuCancelReason reason) {
	if (slot < 0 || slot >= kSlots || !HasClientMenu(slot)) {
		return false; // exactly the sentinel path: 0 == no menu
	}
	if (g_cancel_fails) {
		return false; // simulated s2sdk contract violation: the display stays active
	}
	const s2sdk::MenuId id = *g_client_menu[static_cast<std::size_t>(slot)];
	g_client_menu[static_cast<std::size_t>(slot)].reset();
	if (reason != s2sdk::MenuCancelReason::Disconnect) {
		++g_backend_close_calls;
	}
	Dispatch(id, s2sdk::MenuAction::Cancel, slot, static_cast<int32_t>(reason));
	Dispatch(id, s2sdk::MenuAction::End, slot, 0);
	return true;
}

void Dispatch(s2sdk::MenuId id, s2sdk::MenuAction action, int32_t slot, int32_t param) {
	const auto it = g_menus.find(id);
	if (it == g_menus.end() || it->second.handler == nullptr) {
		return;
	}
	const bool previous = g_in_handler;
	g_in_handler = true;
	it->second.handler(id, action, slot, param);
	g_in_handler = previous;
}

// -- the plugify host ---------------------------------------------------------------------------

void FakeLog(std::string_view message, plg::Severity severity, const plg::source_location& /*location*/) {
	if (severity == plg::Severity::Error) {
		g_errors.emplace_back(message);
	} else if (severity == plg::Severity::Warning) {
		g_warnings.emplace_back(message);
	}
}

plg::string FakeGetConfigsDir() {
	return plg::string(g_configs_dir);
}

plg::string FakeGetDataDir() {
	return plg::string(g_data_dir);
}

plg::uuid FakeGetPluginId(plg::hndl /*handle*/) {
	return static_cast<plg::uuid>(g_plugin_id);
}

// ---------------------------------------------------------------------------------------------
// installing and resetting the fakes
// ---------------------------------------------------------------------------------------------

void ResetFakeRecords() {
	g_fake = FakeState{};
	g_errors.clear();
	g_warnings.clear();
	g_client_calls.clear();
	g_controller_calls.clear();
	g_handle_calls.clear();
	g_schema_lookups.clear();
	g_schema_offsets = {
			{"CCSPlayerPawn::m_flVelocityModifier", 100},
			{"CBaseEntity::m_flGravityScale", 200},
			{"CBaseEntity::m_flActualGravityScale", 300},
	};
	g_float_writes.clear();
	g_server_commands.clear();
	g_printed.clear();
	g_hooks.clear();
	g_hook_calls.clear();
	g_unhook_calls.clear();
	g_on_map_start = nullptr;
	g_on_map_end = nullptr;
	g_map_start_registered = false;
	g_map_end_registered = false;
	g_map_start_unregistered = false;
	g_map_end_unregistered = false;
	g_throw_on_map_end_register = false;
	g_command_calls.clear();
	g_remove_command_calls.clear();
	g_remove_listener_calls.clear();
	g_preexisting_commands.clear();
	g_event_player_slot_calls = 0;
	g_event_int_calls = 0;
	g_cookies.clear();
	g_get_cookie_calls.clear();
	g_set_cookie_calls.clear();
	g_user_loaded = nullptr;
	g_user_loaded_registered = false;
	g_user_loaded_unregistered = false;
	g_user_loaded_register_status = permissions::Status::Success;
	g_plugin_id = 42;
	g_menus.clear();
	g_next_menu_id = 0;
	g_client_menu.fill(std::nullopt);
	g_menu_types = {"chat", "console", "centerhtml", "button"};
	g_menu_calls.clear();
	g_destroyed_menus.clear();
	g_in_handler = false;
	g_destroy_calls_in_handler = 0;
	g_backend_close_calls = 0;
	g_cancel_fails = false;
	g_set_menu_type_fails = false;
	g_type_switches.clear();
	g_hudmenu_choice.fill(std::string());
	g_hudmenu_calls = 0;
	g_timer_calls = 0;
	g_configs_dir = NewTempDir().string();
	g_data_dir = NewTempDir().string();
}

void InstallFakes() {
	// Close a plugin left running by the previous test with the *old* fakes still installed.
	if (g_plugin_started) {
		(void)plg::Plugify_PluginEnd();
		g_plugin_started = false;
	}
	ResetFakeRecords();

	plg::Log = &FakeLog;
	plg::GetConfigsDir = &FakeGetConfigsDir;
	plg::GetDataDir = &FakeGetDataDir;
	plg::plugin::GetId = &FakeGetPluginId;

	__s2sdk_HookEvent = &FakeHookEvent;
	__s2sdk_UnhookEvent = &FakeUnhookEvent;
	__s2sdk_OnMapStart_Register = &FakeOnMapStartRegister;
	__s2sdk_OnMapEnd_Register = &FakeOnMapEndRegister;
	__s2sdk_OnMapStart_Unregister = &FakeOnMapStartUnregister;
	__s2sdk_OnMapEnd_Unregister = &FakeOnMapEndUnregister;
	__s2sdk_AddConsoleCommand = &FakeAddConsoleCommand;
	__s2sdk_RemoveCommand = &FakeRemoveCommand;
	__s2sdk_RemoveCommandListener = &FakeRemoveCommandListener;
	__s2sdk_PlayerSlotToEntPointer = &FakePlayerSlotToEntPointer;
	__s2sdk_PlayerSlotToEntHandle = &FakePlayerSlotToEntHandle;
	__s2sdk_EntPointerToEntHandle = &FakeEntPointerToEntHandle;
	__s2sdk_EntPointerToEntIndex = &FakeEntPointerToEntIndex;
	__s2sdk_IsValidEntPointer = &FakeIsValidEntPointer;
	__s2sdk_IsValidEntHandle = &FakeIsValidEntHandle;
	__s2sdk_IsClientConnected = &FakeIsClientConnected;
	__s2sdk_IsClientInGame = &FakeIsClientInGame;
	__s2sdk_IsClientSourceTV = &FakeIsClientSourceTV;
	__s2sdk_IsFakeClient = &FakeIsFakeClient;
	__s2sdk_IsClientAlive = &FakeIsClientAlive;
	__s2sdk_GetClientSteamID64 = &FakeGetClientSteamID64;
	__s2sdk_GetClientPawn = &FakeGetClientPawn;
	__s2sdk_GetSchemaOffset = &FakeGetSchemaOffset;
	__s2sdk_SetEntDataFloat = &FakeSetEntDataFloat;
	__s2sdk_ServerCommand = &FakeServerCommand;
	__s2sdk_PrintToChat = &FakePrintToChat;
	__s2sdk_GetEventPlayerSlot = &FakeGetEventPlayerSlot;
	__s2sdk_GetEventInt = &FakeGetEventInt;
	__s2sdk_CreateTimer = &FakeCreateTimer;
	__s2sdk_KillsTimer = &FakeKillsTimer;
	__s2sdk_RescheduleTimer = &FakeRescheduleTimer;

	__s2sdk_CreateMenu = &FakeCreateMenu;
	__s2sdk_DestroyMenu = &FakeDestroyMenu;
	__s2sdk_AddMenuItem = &FakeAddMenuItem;
	__s2sdk_SetMenuExitButton = &FakeSetMenuExitButton;
	__s2sdk_SetMenuCloseOnSelect = &FakeSetMenuCloseOnSelect;
	__s2sdk_DisplayMenu = &FakeDisplayMenu;
	__s2sdk_GetMenuItemInfoText = &FakeGetMenuItemInfoText;
	__s2sdk_IsMenuTypeRegistered = &FakeIsMenuTypeRegistered;
	__s2sdk_GetClientMenu = &FakeGetClientMenu;
	__s2sdk_CancelClientMenu = &FakeCancelClientMenu;
	__s2sdk_SetMenuType = &FakeSetMenuType;
	__s2sdk_GetMenuType = &FakeGetMenuType;
	__s2sdk_IsValidMenu = &FakeIsValidMenu;

	__hudmenu_GetClientMenuType = &FakeHudmenuGetClientMenuType;

	__permissions_GetCookie = &FakeGetCookie;
	__permissions_SetCookie = &FakeSetCookie;
	__permissions_OnUserLoaded_Register = &FakeOnUserLoadedRegister;
	__permissions_OnUserLoaded_Unregister = &FakeOnUserLoadedUnregister;
}

// ---------------------------------------------------------------------------------------------
// driving the plugin
// ---------------------------------------------------------------------------------------------

void AddPlayer(int slot, uint64_t steam = 0, bool alive = true) {
	FakeSlot& fake = g_fake.slots[static_cast<std::size_t>(slot)];
	fake.connected = true;
	fake.in_game = true;
	fake.controller = true;
	fake.alive = alive;
	fake.pawn = alive;
	fake.steam = steam;
	// A player taking the slot is a new controller entity with a new serial.
	++fake.generation;
}

void AddBot(int slot, bool alive = true) {
	AddPlayer(slot, 0, alive);
	FakeSlot& fake = g_fake.slots[static_cast<std::size_t>(slot)];
	fake.fake = true;
	fake.steam = 0;
}

void Start() {
	const plg::PluginResult result = plg::Plugify_PluginStart();
	g_plugin_started = true;
	Check(result.GetCode() == plg::PluginCode::Ok, "OnPluginStart answers Ok (never Failed, PITFALLS A1)");
}

void Update(int milliseconds) {
	plg::Plugify_PluginUpdate(std::chrono::milliseconds(milliseconds));
}

void End() {
	(void)plg::Plugify_PluginEnd();
	g_plugin_started = false;
}

void FireMapStart() {
	if (g_on_map_start != nullptr) {
		g_on_map_start();
	}
}

void FireMapEnd() {
	if (g_on_map_end != nullptr) {
		g_on_map_end();
	}
}

s2sdk::ResultType FireEvent(const char* name) {
	const HookRecord* hook = FindHook(name);
	if (hook == nullptr) {
		return s2sdk::ResultType::Continue;
	}
	return hook->callback(plg::string(name), nullptr, false);
}

s2sdk::ResultType CallCommand(const char* name, int32_t caller, s2sdk::ConCommandContext context) {
	const CommandRecord* command = FindCommand(name);
	if (command == nullptr) {
		Check(false, std::string("the command '") + name + "' is not registered");
		return s2sdk::ResultType::Continue;
	}
	const plg::vector<plg::string> args;
	return command->callback(caller, context, args);
}

// The menu manager's SelectMenuItem with closeOnSelect=true: close the display, Select, End.
void SelectMenu(int slot, int index) {
	if (!HasClientMenu(slot)) {
		Check(false, "SelectMenu without a displayed (non-zero id) menu");
		return;
	}
	const s2sdk::MenuId id = *g_client_menu[static_cast<std::size_t>(slot)];
	g_client_menu[static_cast<std::size_t>(slot)].reset();
	Dispatch(id, s2sdk::MenuAction::Select, slot, index);
	Dispatch(id, s2sdk::MenuAction::End, slot, 0);
}

// The menu manager's CancelClientMenu path (exit button).
void ExitMenu(int slot, s2sdk::MenuCancelReason reason = s2sdk::MenuCancelReason::Exit) {
	if (!HasClientMenu(slot)) {
		Check(false, "ExitMenu without a displayed (non-zero id) menu");
		return;
	}
	const s2sdk::MenuId id = *g_client_menu[static_cast<std::size_t>(slot)];
	g_client_menu[static_cast<std::size_t>(slot)].reset();
	// The manager's CloseClientDisplay() calls the backend's close for a real exit; this is the
	// engine's own doing, not the plugin's teardown, so the backend counter is not increased here.
	Dispatch(id, s2sdk::MenuAction::Cancel, slot, static_cast<int32_t>(reason));
	Dispatch(id, s2sdk::MenuAction::End, slot, 0);
}

void OpenViaConsoleMenu(int slot) {
	(void)CallCommand("mm_ret", slot, s2sdk::ConCommandContext::Console);
}

// A running map with the plugin loaded: the common setup of the behaviour tests.
void StartOnMap() {
	WriteConfig(kEnabledConfig);
	Start();
	FireMapStart();
	Update(0);
	Check(g_mapGate.Ready(), "the map gate is open on a started map");
}

// Opens the menu from the console spelling and selects one item, then lets the deferred work run.
void ToggleFromMenu(int slot, int index) {
	OpenViaConsoleMenu(slot);
	if (DisplayedMenu(slot) != 1) {
		Check(false, "the menu is not displayed");
		return;
	}
	SelectMenu(slot, index);
	Update(0);
}

// ---------------------------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------------------------

void TestStartup() {
	Section("startup / registrations (A1)");
	InstallFakes();
	WriteConfig(kEnabledConfig);
	Start();

	CheckEqInt(static_cast<long long>(g_hook_calls.size()), 2, "two event hooks are registered");
	if (const HookRecord* hook = FindHook("round_end"); hook != nullptr) {
		Check(hook->mode == s2sdk::HookMode::Pre, "round_end is a Pre hook");
	} else {
		Check(false, "round_end is hooked");
	}
	if (const HookRecord* hook = FindHook("round_start"); hook != nullptr) {
		Check(hook->mode == s2sdk::HookMode::Pre, "round_start is a Pre hook");
	} else {
		Check(false, "round_start is hooked");
	}
	Check(g_map_start_registered && g_map_end_registered, "both map listeners are registered");

	CheckEqInt(static_cast<long long>(g_command_calls.size()), 2, "two commands are registered");
	if (const CommandRecord* command = FindCommand("mm_ret"); command != nullptr) {
		Check(command->created, "mm_ret is created by this plugin");
		const int64_t flags = static_cast<int64_t>(command->flags);
		Check((flags & static_cast<int64_t>(s2sdk::ConVarFlag::Release)) != 0, "mm_ret has Release");
		Check((flags & static_cast<int64_t>(s2sdk::ConVarFlag::ClientCanExecute)) != 0,
			  "mm_ret has ClientCanExecute");
		Check(command->mode == s2sdk::HookMode::Pre, "mm_ret hooks in Pre mode");
	} else {
		Check(false, "mm_ret is registered");
	}
	Check(FindCommand("ret") != nullptr, "ret (the chat spelling) is registered");
	Check(g_user_loaded_registered, "the permissions OnUserLoaded listener is registered");

	Check(g_settings.bhopEnabled, "config: bhop_enabled 1");
	Check(g_settings.speedEnabled, "config: speed_enabled 1");
	Check(g_settings.gravityEnabled, "config: gravity_enabled 1");
	CheckNear(g_settings.speed, 2.0f, "config: speed 2.0");
	CheckNear(g_settings.gravity, 0.5f, "config: gravity 0.5");

	// PITFALLS B1: nothing client/entity/schema-shaped before the first map.
	Check(g_client_calls.empty(), "no client call in OnPluginStart / before the first map");
	Check(g_schema_lookups.empty(), "no schema lookup before the first map");
	Check(g_float_writes.empty(), "no schema write before the first map");
	// s2sdk's MenuId 0 is both "a valid id" and "no menu": it is burned at start, so the only menu
	// that exists before the first map is the never-displayed reserved dummy.
	Check(g_reservedMenuIdZero, "the unusable MenuId 0 is reserved at start");
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "only the reserved dummy menu exists at start");
	Check(g_menus.count(0) == 1, "the reserved dummy is s2sdk's MenuId 0");
	for (int slot = 0; slot < kSlots; ++slot) {
		if (g_client_menu[static_cast<std::size_t>(slot)]) {
			Check(false, "the reserved dummy is never displayed");
			break;
		}
	}
	CheckEqInt(g_timer_calls, 0, "no s2sdk timer is ever created");

	// A command before the first map must not create a menu either.
	(void)CallCommand("mm_ret", 0, s2sdk::ConCommandContext::Console);
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "mm_ret before the first map opens no menu");

	// The fake models s2sdk's sentinel faithfully: a display with MenuId 0 renders, but the manager
	// stores it in the same state as "no menu", so it cannot be cancelled/selected/destroyed as a
	// display. A plugin regression that displays id 0 would therefore be visible in these tests.
	Check(FakeDisplayMenu(0, 0, 0), "the fake renders a MenuId 0 display like s2sdk");
	Check(!HasClientMenu(0), "MenuId 0 is indistinguishable from \"no menu\" (manager sentinel)");
	Check(!FakeCancelClientMenu(0, s2sdk::MenuCancelReason::Exit), "a MenuId 0 display cannot be cancelled");
	CheckEqInt(g_backend_close_calls, 0, "ending a sentinel display reaches no backend");

	End();
	Check(g_hooks.empty(), "OnPluginEnd unhooks both events");
	Check(g_map_start_unregistered && g_map_end_unregistered, "OnPluginEnd unregisters both map listeners");
	Check(g_user_loaded_unregistered, "OnPluginEnd unregisters the permissions listener");
	Check(g_menus.empty(), "OnPluginEnd destroys the reserved dummy");
	Check(!g_reservedMenuIdZero, "the reservation is released on unload");
	Check(std::find(g_destroyed_menus.begin(), g_destroyed_menus.end(), 0) != g_destroyed_menus.end(),
		  "the reserved dummy is destroyed with DestroyMenu(0)");
}

void TestMapGate() {
	Section("map gate (B1/B2)");
	InstallFakes();
	WriteConfig(kEnabledConfig);
	Start();
	AddPlayer(0, 111);

	// Before any map: the round event is inert (and the convar commands are skipped with it).
	(void)FireEvent("round_end");
	Check(g_server_commands.empty(), "round_end before the first map sends no convar command");
	Check(g_client_calls.empty(), "round_end before the first map makes no client call");
	Check(g_schema_lookups.empty(), "round_end before the first map makes no schema call");

	// A plugin loaded into a running map: its first update is the "map is up" signal.
	Update(0);
	Check(g_mapGate.Ready(), "the first update latches readiness for a mid-map load");
	(void)FireEvent("round_end");
	Check(!g_server_commands.empty(), "round_end works after the mid-map latch");
	Check(!g_float_writes.empty(), "round_end writes the pawn fields after the mid-map latch");

	// Between OnMapEnd and the next OnMapStart nothing may reach a client.
	g_server_commands.clear();
	g_client_calls.clear();
	g_schema_lookups.clear();
	g_float_writes.clear();
	FireMapEnd();
	Check(!g_mapGate.Ready(), "OnMapEnd closes the gate");
	(void)FireEvent("round_end");
	(void)FireEvent("round_start");
	(void)CallCommand("mm_ret", 0, s2sdk::ConCommandContext::Console);
	Update(1000);
	Check(g_client_calls.empty(), "no client call between the maps");
	Check(g_schema_lookups.empty(), "no schema call between the maps");
	Check(g_float_writes.empty(), "no schema write between the maps");
	Check(g_server_commands.empty(), "no convar command between the maps");
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "only the reserved dummy exists between the maps");
	Check(g_get_cookie_calls.empty(), "the cookie reconcile does not run between the maps");

	// The next map opens the gate again.
	FireMapStart();
	Update(0);
	Check(g_mapGate.Ready(), "OnMapStart opens the gate on the next map");
	g_float_writes.clear();
	(void)FireEvent("round_start");
	Check(!g_float_writes.empty(), "round_start writes again on the new map");
	End();

	// An OnMapEnd seen before the first update must not let the latch guess readiness.
	InstallFakes();
	WriteConfig(kEnabledConfig);
	Start();
	FireMapEnd();
	Update(0);
	Check(!g_mapGate.Ready(), "an OnMapEnd before the first update keeps the gate shut");
	Update(1000);
	Check(!g_mapGate.Ready(), "the latch does not reopen a gate that saw OnMapEnd");
	FireMapStart();
	Check(g_mapGate.Ready(), "the next OnMapStart opens it");
	End();

	// Without registered map listeners the gate stays shut and a missing binding is logged.
	InstallFakes();
	WriteConfig(kEnabledConfig);
	Start();
	UnregisterMapListeners();
	Update(0);
	Check(!g_mapGate.Ready(), "without registered map listeners the latch stays shut");
	(void)FireEvent("round_end");
	Check(g_client_calls.empty(), "without the map listeners no client call is made");
	End();

	// A map listener registration that throws is rolled back, the plugin stays usable and Ok.
	InstallFakes();
	WriteConfig(kEnabledConfig);
	g_throw_on_map_end_register = true;
	Start();
	Check(!g_mapGate.ListenersRegistered(), "a failed map-listener registration is rolled back");
	Check(!g_map_start_registered && !g_map_end_registered, "no half-registered map listener pair is left");
	Check(AnyErrorContains("registering the s2sdk map listeners failed"), "the registration failure is logged");
	End();

	// A missing map-listener binding: every client call stays refused, one error line.
	InstallFakes();
	WriteConfig(kEnabledConfig);
	__s2sdk_OnMapStart_Register = nullptr;
	__s2sdk_OnMapEnd_Register = nullptr;
	Start();
	AddPlayer(0, 111);
	Update(1000);
	(void)FireEvent("round_end");
	Check(g_client_calls.empty(), "without the map listener bindings no client call is made");
	Check(AnyErrorContains("map listeners are unavailable"), "the missing map listeners are logged");
	End();
}

void TestRoundEnd() {
	Section("round_end / round_start writes");
	InstallFakes();
	StartOnMap();

	AddPlayer(0, 111);				   // normal alive player
	AddPlayer(1, 112, /*alive=*/false); // dead
	AddBot(2);						   // a bot's pawn is boosted too (the original did not skip bots)
	g_fake.slots[3].connected = true;
	g_fake.slots[3].in_game = true;
	g_fake.slots[3].source_tv = true; // SourceTV
	AddPlayer(4, 114);
	g_fake.slots[4].controller = false; // no controller
	AddPlayer(5, 115);
	g_fake.slots[5].handle = -1; // INVALID_EHANDLE_INDEX
	AddPlayer(6, 116);
	g_fake.slots[6].handle = -20008; // a valid negative CEntityHandle (serial into bit 31)
	AddPlayer(7, 117);
	g_fake.slots[7].pawn = false; // controller but no pawn
	// slot 8: not connected at all
	AddPlayer(9, 119);
	g_players[9].disableSpeed = true; // opted out of the speed boost
	g_players[9].disableGravity = true;

	(void)FireEvent("round_end");

	CheckEqInt(static_cast<long long>(g_server_commands.size()), 2, "round_end sends the two bhop convars");
	if (g_server_commands.size() == 2) {
		CheckEqStr(g_server_commands[0], "sv_autobunnyhopping 1", "bhop convar 1");
		CheckEqStr(g_server_commands[1], "sv_enablebunnyhopping 1", "bhop convar 2");
	}

	// The alive players get three writes each: velocity, gravity and actual gravity.
	CheckEqInt(CountFloatWrites(2000), 3, "slot 0 gets three field writes");
	CheckEqInt(CountFloatWrites(2002), 3, "the bot gets three field writes");
	CheckEqInt(CountFloatWrites(-20008), 3, "a valid negative handle is written");
	CheckEqInt(CountFloatWrites(2009), 3, "the opted-out player still gets the restored values");
	CheckEqInt(CountFloatWrites(2001), 0, "the dead player is skipped");
	CheckEqInt(CountFloatWrites(2004), 0, "the slot without a controller is skipped");
	CheckEqInt(CountFloatWrites(-1), 0, "the -1 handle is refused");
	CheckEqInt(CountFloatWrites(2007), 0, "the slot without a pawn is skipped");
	CheckEqInt(CountFloatWrites(2008), 0, "the disconnected slot is skipped");

	const FloatWrite* velocity = FindFloatWrite(2000, 100);
	const FloatWrite* gravity = FindFloatWrite(2000, 200);
	const FloatWrite* actual = FindFloatWrite(2000, 300);
	Check(velocity != nullptr && gravity != nullptr && actual != nullptr, "slot 0 got the three offsets");
	if (velocity != nullptr) {
		CheckNear(velocity->value, 2.0f, "slot 0 velocity = config speed");
		CheckEqInt(velocity->size, 4, "velocity is written as a 4-byte float");
		Check(!velocity->change_state, "velocity is written without a network state change");
		CheckEqInt(velocity->chain_offset, -2, "the inert chain offset is passed");
	}
	if (gravity != nullptr) {
		CheckNear(gravity->value, 0.5f, "slot 0 gravity = config gravity");
	}
	if (actual != nullptr) {
		CheckNear(actual->value, 0.5f, "slot 0 actual gravity = config gravity");
	}
	const FloatWrite* optedVelocity = FindFloatWrite(2009, 100);
	const FloatWrite* optedGravity = FindFloatWrite(2009, 200);
	Check(optedVelocity != nullptr && optedVelocity->value == 1.0f, "the speed opt-out gets 1.0");
	Check(optedGravity != nullptr && optedGravity->value == 1.0f, "the gravity opt-out gets 1.0");

	// C8: the offsets are resolved once and cached.
	CheckEqInt(static_cast<long long>(g_schema_lookups.size()), 3, "three schema lookups on the first round_end");
	g_float_writes.clear();
	g_schema_lookups.clear();
	(void)FireEvent("round_end");
	Check(g_schema_lookups.empty(), "the cached offsets are reused on the next round_end");

	// round_start restores 1.0 and switches the convars back.
	g_server_commands.clear();
	g_float_writes.clear();
	(void)FireEvent("round_start");
	CheckEqInt(static_cast<long long>(g_server_commands.size()), 2, "round_start sends the two bhop convars");
	if (g_server_commands.size() == 2) {
		CheckEqStr(g_server_commands[0], "sv_autobunnyhopping 0", "bhop convar 1 off");
		CheckEqStr(g_server_commands[1], "sv_enablebunnyhopping 0", "bhop convar 2 off");
	}
	const FloatWrite* restored = FindFloatWrite(2000, 100);
	Check(restored != nullptr && restored->value == 1.0f, "round_start restores velocity to 1.0");
	const FloatWrite* restoredGravity = FindFloatWrite(2000, 200);
	Check(restoredGravity != nullptr && restoredGravity->value == 1.0f, "round_start restores gravity to 1.0");
	const FloatWrite* restoredActual = FindFloatWrite(2000, 300);
	Check(restoredActual != nullptr && restoredActual->value == 1.0f, "round_start restores actual gravity to 1.0");

	// The feature flags gate the boost but never the restoration.
	g_settings.speedEnabled = false;
	g_settings.gravityEnabled = false;
	g_float_writes.clear();
	(void)FireEvent("round_end");
	const FloatWrite* unboosted = FindFloatWrite(2000, 100);
	Check(unboosted != nullptr && unboosted->value == 1.0f, "speed_enabled 0 writes the default");
	const FloatWrite* unboostedGravity = FindFloatWrite(2000, 200);
	Check(unboostedGravity != nullptr && unboostedGravity->value == 1.0f, "gravity_enabled 0 writes the default");

	g_settings.bhopEnabled = false;
	g_server_commands.clear();
	g_float_writes.clear();
	(void)FireEvent("round_end");
	Check(g_server_commands.empty(), "bhop_enabled 0 sends no convar command");
	Check(FindFloatWrite(2000, 100) != nullptr, "bhop_enabled 0 keeps the field writes");

	// The two events carry no player field: the player-slot/int accessors are never needed.
	CheckEqInt(g_event_player_slot_calls, 0, "GetEventPlayerSlot is never called");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");

	End();
}

void TestSchemaFailures() {
	Section("schema offsets (C6/C8)");
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);

	// A failed lookup is not cached and is retried on the next round event.
	g_schema_offsets["CCSPlayerPawn::m_flVelocityModifier"] = -1;
	(void)FireEvent("round_end");
	Check(FindFloatWrite(2000, 100) == nullptr, "an unavailable velocity field is not written");
	Check(FindFloatWrite(2000, 200) != nullptr, "the other fields are still written");
	Check(AnyErrorContains("schema field(s) not found"), "the missing schema field is logged");

	g_schema_offsets["CCSPlayerPawn::m_flVelocityModifier"] = 100;
	g_float_writes.clear();
	(void)FireEvent("round_end");
	Check(FindFloatWrite(2000, 100) != nullptr, "the field is resolved on the next round_end");
	End();

	// Without the schema accessors nothing is written and nothing crashes.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_GetSchemaOffset = nullptr;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "without GetSchemaOffset no field is written");
	Check(!g_server_commands.empty(), "the convar commands still run");

	__s2sdk_SetEntDataFloat = nullptr;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "without SetEntDataFloat no field is written");
	End();

	// Without GetClientPawn / EntPointerToEntHandle nothing is written either.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_GetClientPawn = nullptr;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "without GetClientPawn no field is written");
	End();

	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_EntPointerToEntHandle = nullptr;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "without EntPointerToEntHandle no field is written");
	End();

	// The client guards refuse an invalid controller pointer/handle without calling the writer.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	g_fake.valid_pointer = false;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "an invalid controller pointer is refused");
	End();

	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	g_fake.handle_validity[ControllerHandleValue(0)] = false;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "an engine-rejected controller handle is refused");
	End();
}

void TestConfig() {
	Section("config paths / startup reload");
	InstallFakes();
	// The raw cwd-relative Metamod file wins over the configs copy (pisexcompat order).
	const std::filesystem::path cwd = NewTempDir();
	WriteTextFile((cwd / "addons/configs/RoundEndThings/settings.ini").string(),
				  "\"Config\"\n{\n    \"speed\" \"3.5\"\n    \"speed_enabled\" \"0\"\n}\n");
	WriteConfig("\"Config\"\n{\n    \"speed\" \"9.0\"\n}\n");
	{
		const ScopedCwd scoped(cwd);
		Start();
	}
	CheckNear(g_settings.speed, 3.5f, "the raw cwd settings.ini wins");
	Check(!g_settings.speedEnabled, "the raw cwd settings.ini wins (speed_enabled)");
	End();

	// A malformed first-existing file keeps the defaults and is logged.
	InstallFakes();
	{
		const std::filesystem::path rawCwd = NewTempDir();
		WriteTextFile((rawCwd / "addons/configs/RoundEndThings/settings.ini").string(),
					  "\"Config\"\n{\n    \"speed\" \"unterminated\n");
		const ScopedCwd scoped(rawCwd);
		Start();
	}
	CheckNear(g_settings.speed, 1.5f, "a malformed raw file keeps the built-in default");
	Check(AnyErrorContains("failed to parse"), "the malformed file is reported as a parse failure");
	End();

	// The production file is read unchanged by the plugin-local KV1 reader.
	InstallFakes();
	std::error_code copyError;
	std::filesystem::create_directories(std::filesystem::path(g_configs_dir) / "RoundEndThings", copyError);
	std::filesystem::copy_file(std::filesystem::path(ROUNDEND_TEST_DATA_DIR) / "RoundEndThings/settings.ini",
							   std::filesystem::path(g_configs_dir) / "RoundEndThings/settings.ini",
							   std::filesystem::copy_options::overwrite_existing, copyError);
	Start();
	Check(g_settings.bhopEnabled, "production: bhop_enabled 1");
	Check(!g_settings.speedEnabled, "production: speed_enabled 0");
	Check(!g_settings.gravityEnabled, "production: gravity_enabled 0");
	CheckNear(g_settings.speed, 1.0f, "production: speed 1.0");
	End();

	// No file at all: the defaults are kept and the missing path is reported.
	InstallFakes();
	Start();
	CheckNear(g_settings.speed, 1.5f, "a missing file keeps the built-in default");
	CheckNear(g_settings.gravity, 1.5f, "a missing file keeps the built-in gravity default");
	Check(g_settings.bhopEnabled && g_settings.speedEnabled && g_settings.gravityEnabled,
		  "a missing file keeps all features enabled");
	Check(AnyErrorContains("failed to load"), "the missing file is reported as a load failure");

	// The original's StartupServer re-read happens once, on the first update with an open gate.
	WriteConfig("\"Config\"\n{\n    \"speed\" \"4.0\"\n}\n");
	Update(0);
	CheckNear(g_settings.speed, 4.0f, "the first update with an open gate re-reads the config");
	WriteConfig("\"Config\"\n{\n    \"speed\" \"6.0\"\n}\n");
	Update(0);
	CheckNear(g_settings.speed, 4.0f, "the startup re-read happens only once");
	FireMapStart();
	Update(0);
	CheckNear(g_settings.speed, 4.0f, "a later map does not re-read the config again");
	End();
}

void TestCommands() {
	Section("commands / menu content (C11)");
	InstallFakes();
	g_menu_types.insert("hud"); // the production backend is installed
	StartOnMap();
	AddPlayer(0, 111);

	// Context filtering: "mm_ret" answers Console only, "ret" answers Chat only.
	Check(CallCommand("mm_ret", 0, s2sdk::ConCommandContext::Chat) == s2sdk::ResultType::Continue,
		  "a chat !mm_ret is not handled");
	CheckEqInt(DisplayedMenu(0), 0, "a chat !mm_ret opens no menu");
	Check(CallCommand("ret", 0, s2sdk::ConCommandContext::Console) == s2sdk::ResultType::Continue,
		  "a console ret is not handled");
	CheckEqInt(DisplayedMenu(0), 0, "a console ret opens no menu");

	// The server console has no menu (the original's callback had no player slot either).
	Check(CallCommand("mm_ret", -1, s2sdk::ConCommandContext::Console) == s2sdk::ResultType::Handled,
		  "the server console mm_ret is consumed");
	CheckEqInt(DisplayedMenu(0), 0, "the server console opens no menu");

	// The console spelling opens the production "hud" menu.
	Check(CallCommand("mm_ret", 0, s2sdk::ConCommandContext::Console) == s2sdk::ResultType::Handled,
		  "mm_ret is handled");
	// The reserved dummy occupies MenuId 0, so the first real menu is id 1: s2sdk never confuses it
	// with the "no menu" client state.
	Check(g_client_menu[0].has_value() && *g_client_menu[0] == 1,
		  "the first real menu has id 1 (id 0 is reserved)");
	CheckEqInt(DisplayedMenu(0), 1, "the menu is displayed on the caller's slot");
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqStr(menu->title, "Настройки конца раунда", "the original menu title");
		CheckEqStr(menu->type, "hud", "the hud backend is preferred when registered");
		CheckEqInt(static_cast<long long>(menu->items.size()), 2, "two toggle items when both features are on");
		if (menu->items.size() == 2) {
			CheckEqStr(menu->items[0].first, "speed", "the speed item info");
			CheckEqStr(menu->items[0].second, "Отключить скорость [ ]", "the speed item label");
			CheckEqStr(menu->items[1].first, "gravity", "the gravity item info");
			CheckEqStr(menu->items[1].second, "Отключить гравитацию [ ]", "the gravity item label");
		}
		Check(menu->exit_button, "the exit button is enabled (SetExitMenu of the original)");
		Check(menu->close_on_select, "close-on-select is explicit (the original closed and re-opened)");
	} else {
		Check(false, "the menu is displayed");
	}
	ExitMenu(0);
	Update(0);
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "the exit destroys the menu, the reserved dummy stays");

	// The chat spelling works and, without hudmenu's backend, the original's HUD_LAYOUT falls back
	// to s2sdk's on-screen WASD "button" (pisexcompat ResolveS2sdkMenuTypeName).
	g_menu_types.erase("hud");
	Check(CallCommand("ret", 0, s2sdk::ConCommandContext::Chat) == s2sdk::ResultType::Handled,
		  "!ret is handled");
	CheckEqInt(DisplayedMenu(0), 1, "!ret displays the menu");
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqStr(menu->type, "button", "the WASD button backend is the no-hudmenu fallback");
	}
	ExitMenu(0);
	Update(0);

	// The menu follows the enabled features.
	g_settings.speedEnabled = false;
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 1, "the menu opens with speed disabled");
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqInt(static_cast<long long>(menu->items.size()), 1, "only the gravity item is shown");
		if (menu->items.size() == 1) {
			CheckEqStr(menu->items[0].first, "gravity", "the remaining item is gravity");
		}
	}
	ExitMenu(0);
	Update(0);

	g_settings.speedEnabled = true;
	g_settings.gravityEnabled = false;
	OpenViaConsoleMenu(0);
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqInt(static_cast<long long>(menu->items.size()), 1, "only the speed item is shown");
		if (menu->items.size() == 1) {
			CheckEqStr(menu->items[0].first, "speed", "the remaining item is speed");
		}
	}
	ExitMenu(0);
	Update(0);

	g_settings.speedEnabled = false;
	g_settings.gravityEnabled = false;
	OpenViaConsoleMenu(0);
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqInt(static_cast<long long>(menu->items.size()), 0, "no toggle item when both features are off");
		Check(menu->exit_button, "the exit button is still there");
	}
	ExitMenu(0);
	Update(0);
	End();

	// Command ownership: a pre-existing "ret" is only a listener on teardown.
	InstallFakes();
	g_preexisting_commands.insert("ret");
	StartOnMap();
	if (const CommandRecord* command = FindCommand("ret"); command != nullptr) {
		Check(!command->created, "a pre-existing ret is not created by this plugin");
	} else {
		Check(false, "ret is attached even when the name exists");
	}
	End();
	CheckEqInt(static_cast<long long>(g_remove_command_calls.size()), 1,
			   "only mm_ret is removed with RemoveCommand");
	if (!g_remove_command_calls.empty()) {
		CheckEqStr(g_remove_command_calls[0].name, "mm_ret", "RemoveCommand is called for mm_ret");
	}
	CheckEqInt(static_cast<long long>(g_remove_listener_calls.size()), 1,
			   "the pre-existing ret is detached with RemoveCommandListener");
	if (!g_remove_listener_calls.empty()) {
		CheckEqStr(g_remove_listener_calls[0].name, "ret", "RemoveCommandListener is called for ret");
		Check(g_remove_listener_calls[0].mode == s2sdk::HookMode::Pre, "the listener mode matches");
	}

	// Without AddConsoleCommand no command exists and teardown stays quiet.
	InstallFakes();
	__s2sdk_AddConsoleCommand = nullptr;
	Start();
	Check(g_command_calls.empty(), "without AddConsoleCommand nothing is registered");
	Check(AnyErrorContains("command bindings are unavailable"), "the missing command binding is logged");
	End();
	Check(g_remove_command_calls.empty() && g_remove_listener_calls.empty(),
		  "nothing is removed when nothing was registered");

	// Without the full menu binding set the commands still answer their context but open nothing.
	// The cleanup half (GetClientMenu/CancelClientMenu) belongs to that set: without it a displayed
	// menu could not be ended safely at a map change, so no menu may be created at all.
	for (const char* missing : {"CreateMenu", "GetClientMenu", "CancelClientMenu", "SetMenuType", "GetMenuType"}) {
		InstallFakes();
		StartOnMap();
		AddPlayer(0, 111);
		if (std::string_view(missing) == "CreateMenu") {
			__s2sdk_CreateMenu = nullptr;
		} else if (std::string_view(missing) == "GetClientMenu") {
			__s2sdk_GetClientMenu = nullptr;
		} else if (std::string_view(missing) == "CancelClientMenu") {
			__s2sdk_CancelClientMenu = nullptr;
		} else if (std::string_view(missing) == "SetMenuType") {
			__s2sdk_SetMenuType = nullptr;
		} else {
			__s2sdk_GetMenuType = nullptr;
		}
		Check(CallCommand("mm_ret", 0, s2sdk::ConCommandContext::Console) == s2sdk::ResultType::Handled,
			  std::string("mm_ret is still handled without ") + missing);
		CheckEqInt(DisplayedMenu(0), 0, std::string("no menu is displayed without ") + missing);
		Check(AnyErrorContains("menu bindings are unavailable"),
			  std::string("the missing ") + missing + " binding is logged");
		const std::size_t errorsAfterFirst = g_errors.size();
		(void)CallCommand("mm_ret", 0, s2sdk::ConCommandContext::Console);
		CheckEqInt(static_cast<long long>(g_errors.size()), static_cast<long long>(errorsAfterFirst),
				   std::string("the missing ") + missing + " binding is logged only once");
		End();
	}
}

void TestMenuToggles() {
	Section("menu toggles: deferred apply / identity (C10)");
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);

	// Opening the menu refreshes the slot identity itself (no reconcile needed before the click).
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 1, "the menu is displayed");
	Check(g_client_menu[0].has_value() && *g_client_menu[0] == 1,
		  "the first real menu has id 1 (MenuId 0 is reserved)");

	// The handler only records the intent: nothing is toggled, printed or saved before the update.
	SelectMenu(0, 0);
	Check(!g_players[0].disableSpeed, "the handler does not change the settings");
	Check(g_printed.empty(), "the handler prints no chat line");
	Check(g_set_cookie_calls.empty(), "the handler writes no cookie");
	CheckEqInt(g_destroy_calls_in_handler, 0, "DestroyMenu is never called inside the handler");
	CheckEqInt(static_cast<long long>(g_destroyed_menus.size()), 0, "the menu is not destroyed in the handler");
	CheckEqInt(DisplayedMenu(0), 0, "the closed menu is no longer displayed");

	// The update applies the recorded selection after re-checking the owner identity, then saves
	// and re-opens the menu.
	Update(0);
	Check(g_players[0].disableSpeed, "the update applies the speed selection");
	Check(!g_players[0].disableGravity, "the gravity flag is untouched");
	CheckEqInt(static_cast<long long>(g_printed.size()), 1, "one chat reply is printed by the update");
	if (!g_printed.empty()) {
		CheckEqInt(g_printed[0].slot, 0, "the reply goes to the player");
		CheckEqStr(g_printed[0].message, "Настройка скорости изменена.", "the original speed reply");
	}
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 2, "both cookies are saved by the update");
	if (g_set_cookie_calls.size() == 2) {
		CheckEqInt(g_set_cookie_calls[0].plugin_id, 42, "the plugin id comes from the host");
		CheckEqInt(static_cast<long long>(g_set_cookie_calls[0].target), 111, "the cookie is keyed by steamid");
		CheckEqStr(g_set_cookie_calls[0].name, "roundend_disable_speed", "the speed cookie name");
		Check(plg::holds_alternative<int64_t>(g_set_cookie_calls[0].value), "the speed cookie value is int64");
		Check(plg::get<int64_t>(g_set_cookie_calls[0].value) == 1, "the speed cookie value is 1");
		CheckEqStr(g_set_cookie_calls[1].name, "roundend_disable_gravity", "the gravity cookie name");
		Check(plg::get<int64_t>(g_set_cookie_calls[1].value) == 0, "the gravity cookie value is 0");
		Check(!g_set_cookie_calls[0].dont_broadcast, "the cookie is saved with dontBroadcast=false");
	}
	CheckEqInt(static_cast<long long>(g_destroyed_menus.size()), 1, "the update destroys the closed menu");
	CheckEqInt(DisplayedMenu(0), 1, "the update re-opens the menu");
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqStr(menu->items[0].second, "Отключить скорость [X]", "the re-opened label shows [X]");
		CheckEqStr(menu->items[1].second, "Отключить гравитацию [ ]", "the gravity label is unchanged");
	} else {
		Check(false, "the re-opened menu is displayed");
	}

	// Selecting gravity on the re-opened menu.
	SelectMenu(0, 1);
	Update(0);
	Check(g_players[0].disableGravity, "the update applies the gravity selection");
	Check(!g_printed.empty() && g_printed.back().message == "Настройка гравитации изменена.",
		  "the original gravity reply");
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqStr(menu->items[1].second, "Отключить гравитацию [X]", "the gravity label shows [X]");
	}

	// Selecting speed again flips it back.
	SelectMenu(0, 0);
	Update(0);
	Check(!g_players[0].disableSpeed, "the update re-enables the speed boost");
	if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
		CheckEqStr(menu->items[0].second, "Отключить скорость [ ]", "the speed label is back to [ ]");
	}

	// Exit: the menu is destroyed and not re-opened.
	ExitMenu(0);
	Update(0);
	CheckEqInt(DisplayedMenu(0), 0, "exit does not re-open the menu");
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "the exited menu is destroyed (the dummy stays)");
	End();

	// A slot reused between the selection and the update never receives the previous player's
	// change: the identity (SteamID64 + controller serial) no longer matches, so the intent is
	// dropped with no toggle, no chat line, no cookie write and no re-open.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	SelectMenu(0, 0);
	AddPlayer(0, 222); // new connected player in the same slot, new controller entity
	const std::size_t chatsBefore = g_printed.size();
	g_set_cookie_calls.clear();
	Update(0);
	Check(!g_players[0].disableSpeed, "the previous occupant's selection is dropped");
	CheckEqInt(static_cast<long long>(g_printed.size()), static_cast<long long>(chatsBefore),
			   "no chat line is sent to the new occupant");
	Check(g_set_cookie_calls.empty(), "no cookie is written for the new occupant");
	CheckEqInt(DisplayedMenu(0), 0, "the menu is not re-opened for the new occupant");
	End();

	// A slot whose client is gone before the update is dropped as well (no stale cookie).
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	SelectMenu(0, 0);
	g_fake.slots[0].connected = false;
	g_fake.slots[0].steam = 0;
	g_printed.clear();
	g_set_cookie_calls.clear();
	Update(0);
	Check(!g_players[0].disableSpeed, "a vanished client's selection is not applied");
	Check(g_printed.empty(), "nothing is printed when the client is gone");
	Check(g_set_cookie_calls.empty(), "no stale cookie is written when the client is gone");
	End();

	// The same SteamID64 with a different controller entity (slot reused by the same account) is a
	// different owner as well: the controller handle/serial is part of the identity.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	SelectMenu(0, 0);
	AddPlayer(0, 111); // same account, new controller entity/serial
	Update(0);
	Check(!g_players[0].disableSpeed, "a new controller serial drops the previous selection");
	End();

	// Teardown with a live map: DestroyMenu may clear the viewer through the backend (legal).
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 1, "a menu is open before the unload");
	CheckEqInt(g_backend_close_calls, 0, "opening a menu calls no backend close");
	End();
	Check(g_menus.empty(), "OnPluginEnd destroys every menu it created");
	Check(!g_client_menu[0].has_value(), "the client display is gone after the unload");
	CheckEqInt(g_destroy_calls_in_handler, 0, "the teardown destroy runs outside the handler");
	CheckEqInt(g_backend_close_calls, 0,
			   "the teardown ends the display with the Disconnect reason, so it never calls the backend");
}

void TestPermissions() {
	Section("permissions cookies (reconcile / reuse)");
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	g_cookies["111:roundend_disable_speed"] = plg::any(plg::string("1"));
	g_cookies["111:roundend_disable_gravity"] = plg::any(static_cast<int64_t>(0));

	// The reconcile is a one-second accumulator; nothing runs before the interval.
	Update(999);
	Check(g_get_cookie_calls.empty(), "the reconcile does not run before one second");
	Update(1);
	CheckEqInt(static_cast<long long>(g_get_cookie_calls.size()), 2, "both cookies are read on the reconcile");
	Check(g_players[0].disableSpeed, "the string \"1\" loads as disabled");
	Check(!g_players[0].disableGravity, "the int64 0 loads as enabled");

	// The loaded opt-out is honoured by round_end.
	g_float_writes.clear();
	(void)FireEvent("round_end");
	const FloatWrite* velocity = FindFloatWrite(2000, 100);
	Check(velocity != nullptr && velocity->value == 1.0f, "the loaded speed opt-out writes 1.0");
	const FloatWrite* gravity = FindFloatWrite(2000, 200);
	Check(gravity != nullptr && gravity->value == 0.5f, "the loaded gravity setting boosts");
	g_float_writes.clear();

	// A missing cookie counts as loaded with the default (CookieNotFound).
	AddPlayer(1, 112);
	Update(1000);
	Check(!g_players[1].disableSpeed && !g_players[1].disableGravity, "CookieNotFound keeps the defaults");

	// An unusable value is reported and keeps the default.
	AddPlayer(2, 113);
	g_cookies["113:roundend_disable_speed"] = plg::any(plg::string("yes"));
	Update(1000);
	Check(!g_players[2].disableSpeed, "an unusable cookie keeps the default");
	Check(AnyWarningContains("unusable"), "the unusable cookie is reported");

	// TargetUserNotFound is retried by the next reconcile instead of marking the slot loaded.
	AddPlayer(3, 114);
	g_fake.get_cookie_status_by_name["roundend_disable_speed"] = permissions::Status::TargetUserNotFound;
	Update(1000);
	const std::size_t retriedBefore = g_get_cookie_calls.size();
	Update(1000);
	Check(g_get_cookie_calls.size() > retriedBefore, "a TargetUserNotFound is retried");
	g_fake.get_cookie_status_by_name.clear();
	g_cookies["114:roundend_disable_speed"] = plg::any(static_cast<int32_t>(1));
	Update(1000);
	Check(g_players[3].disableSpeed, "the retry loads the cookie once the user exists");

	// A throwing GetCookie is caught and logged, the reconcile continues with the other slots.
	AddPlayer(4, 115);
	g_fake.throw_on_get_cookie = true;
	Update(1000);
	Check(AnyErrorContains("reading the round-end cookies failed"), "a throwing GetCookie is caught");
	g_fake.throw_on_get_cookie = false;

	// The menu toggle writes both cookies with the plugin id, steamid, name and int64 value.
	AddPlayer(5, 116);
	Update(1000);
	g_set_cookie_calls.clear();
	ToggleFromMenu(5, 0);
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 2, "the toggle saves both cookies");
	if (g_set_cookie_calls.size() == 2) {
		CheckEqInt(g_set_cookie_calls[0].plugin_id, 42, "SetCookie uses the host plugin id");
		CheckEqInt(static_cast<long long>(g_set_cookie_calls[0].target), 116, "SetCookie uses the steamid");
		CheckEqStr(g_set_cookie_calls[0].name, "roundend_disable_speed", "SetCookie speed name");
		Check(plg::get<int64_t>(g_set_cookie_calls[0].value) == 1, "SetCookie speed value 1");
		CheckEqStr(g_set_cookie_calls[1].name, "roundend_disable_gravity", "SetCookie gravity name");
		Check(plg::get<int64_t>(g_set_cookie_calls[1].value) == 0, "SetCookie gravity value 0");
	}
	Check(!g_slots[5].pendingSave, "a successful save clears the pending flag");
	if (DisplayedMenu(5) == 1) {
		ExitMenu(5);
		Update(0);
	}

	// A failed save stays pending and is retried by the reconcile.
	AddPlayer(6, 117);
	Update(1000);
	g_set_cookie_calls.clear();
	g_fake.set_cookie_failures_remaining = 1;
	ToggleFromMenu(6, 0);
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 1, "the first save fails after one cookie");
	Check(g_slots[6].pendingSave, "a failed save stays pending");
	Update(1000);
	Check(g_set_cookie_calls.size() >= 3, "the reconcile retries the failed save");
	Check(!g_slots[6].pendingSave, "the retried save succeeds");
	if (DisplayedMenu(6) == 1) {
		ExitMenu(6);
		Update(0);
	}

	// The permissions OnUserLoaded listener retries the save of the matching slot.
	AddPlayer(7, 118);
	Update(1000);
	g_set_cookie_calls.clear();
	g_fake.set_cookie_failures_remaining = 1;
	ToggleFromMenu(7, 0);
	Check(g_slots[7].pendingSave, "the save is pending before the user-loaded event");
	g_set_cookie_calls.clear();
	if (g_user_loaded != nullptr) {
		g_user_loaded(42, 118, permissions::PlayerState::Online);
	}
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 2, "OnUserLoaded retries the save");
	Check(!g_slots[7].pendingSave, "the save succeeds from OnUserLoaded");
	if (DisplayedMenu(7) == 1) {
		ExitMenu(7);
		Update(0);
	}

	// A pending save whose slot lost its client is dropped: writing it would store the pending
	// state under the previous occupant's SteamID64 after they are gone.
	AddPlayer(12, 611);
	Update(1000);
	ToggleFromMenu(12, 0);
	Check(!g_slots[12].pendingSave, "the normal save completed first");
	g_slots[12].pendingSave = true;
	g_slots[12].pendingFeatures = kPendingSpeed;
	g_fake.slots[12].connected = false;
	g_fake.slots[12].steam = 0;
	g_set_cookie_calls.clear();
	if (g_user_loaded != nullptr) {
		g_user_loaded(42, 611, permissions::PlayerState::Online);
	}
	Check(g_set_cookie_calls.empty(), "a pending save with no client in the slot writes no cookie");
	Check(!g_slots[12].pendingSave, "the stale pending save is dropped");
	if (DisplayedMenu(12) == 1) {
		ExitMenu(12);
		Update(0);
	}

	// Slot reuse: the new occupant starts from the defaults and loads their own cookies.
	AddPlayer(8, 211);
	g_cookies["211:roundend_disable_speed"] = plg::any(static_cast<int64_t>(1));
	Update(1000);
	Check(g_players[8].disableSpeed, "the first occupant's cookie loaded");
	g_fake.slots[8].steam = 212;
	g_cookies["212:roundend_disable_speed"] = plg::any(static_cast<int64_t>(0));
	g_cookies["212:roundend_disable_gravity"] = plg::any(static_cast<int64_t>(0));
	Update(1000);
	Check(!g_players[8].disableSpeed, "the reused slot is reset and loads the new cookie");
	Check(!g_players[8].disableGravity, "the reused slot keeps the new gravity cookie");

	// A stale pending save is dropped when the slot changes hands before the reconcile.
	AddPlayer(9, 311);
	Update(1000);
	g_slots[9].pendingSave = true;
	g_fake.slots[9].steam = 312;
	g_set_cookie_calls.clear();
	Update(1000);
	Check(g_set_cookie_calls.empty(), "a stale save is dropped when the slot changed hands");

	// Opening the menu loads the stored cookies first: the labels show what the player actually has
	// and a toggle saves the *loaded* value of the other cookie instead of the default.
	AddPlayer(10, 411);
	g_cookies["411:roundend_disable_speed"] = plg::any(static_cast<int64_t>(1));
	g_cookies["411:roundend_disable_gravity"] = plg::any(static_cast<int64_t>(1));
	g_players[10] = roundend::PlayerSettings{};
	g_set_cookie_calls.clear();
	OpenViaConsoleMenu(10);
	Check(g_players[10].disableSpeed && g_players[10].disableGravity,
		  "opening the menu loads the stored cookies");
	if (const FakeMenu* menu = DisplayedMenuOf(10); menu != nullptr) {
		Check(menu->items.size() == 2 && menu->items[0].second == "Отключить скорость [X]" &&
				  menu->items[1].second == "Отключить гравитацию [X]",
			  "the menu labels show the stored [X] state");
	}
	SelectMenu(10, 0); // flip speed off; the loaded gravity opt-out must be kept
	Update(0);
	Check(!g_players[10].disableSpeed, "the toggle flips the selected feature");
	Check(g_players[10].disableGravity, "the loaded other feature is kept");
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 2, "both cookies are saved after the toggle");
	if (g_set_cookie_calls.size() == 2) {
		Check(plg::get<int64_t>(g_set_cookie_calls[0].value) == 0, "the toggled speed cookie is saved as 0");
		Check(plg::get<int64_t>(g_set_cookie_calls[1].value) == 1, "the loaded gravity cookie is saved as 1");
	}
	if (DisplayedMenu(10) == 1) {
		ExitMenu(10);
		Update(0);
	}

	// A permissions user that is not loaded yet: the pending choice stays authoritative, and the
	// later load merges the stored *other* cookie instead of overwriting the choice.
	AddPlayer(11, 511);
	g_cookies["511:roundend_disable_gravity"] = plg::any(static_cast<int64_t>(1));
	g_fake.get_cookie_status_by_name["roundend_disable_speed"] = permissions::Status::TargetUserNotFound;
	g_fake.set_cookie_status_by_name["roundend_disable_speed"] = permissions::Status::TargetUserNotFound;
	OpenViaConsoleMenu(11);
	SelectMenu(11, 0); // the speed toggle; the save fails because the user is not loaded
	Update(0);		   // the deferred re-open must show the pending [X] label
	Check(g_players[11].disableSpeed, "the pending toggle survives the failed save");
	if (const FakeMenu* menu = DisplayedMenuOf(11); menu != nullptr) {
		Check(menu->items.size() == 2 && menu->items[0].second == "Отключить скорость [X]",
			  "the re-opened menu keeps the pending [X] label");
	}
	g_fake.get_cookie_status_by_name.clear();
	g_fake.set_cookie_status_by_name.clear();
	g_set_cookie_calls.clear();
	Update(1000); // the reconcile now loads the user and saves the merged cookies
	Check(g_slots[11].loaded, "the slot is loaded after the delayed user load");
	Check(g_players[11].disableSpeed, "the pending speed choice survives the delayed load");
	Check(g_players[11].disableGravity, "the untouched stored gravity cookie is merged in");
	Check(!g_slots[11].pendingSave, "the merged save succeeds");
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 2, "both cookies are saved after the merge");
	if (g_set_cookie_calls.size() == 2) {
		Check(plg::get<int64_t>(g_set_cookie_calls[0].value) == 1, "the chosen speed cookie is saved as 1");
		Check(plg::get<int64_t>(g_set_cookie_calls[1].value) == 1, "the merged gravity cookie is saved as 1");
	}
	if (DisplayedMenu(11) == 1) {
		ExitMenu(11);
		Update(0);
	}
	End();

	// Absent permissions bindings disable only the cookie feature.
	InstallFakes();
	__permissions_GetCookie = nullptr;
	StartOnMap();
	AddPlayer(0, 111);
	Check(AnyWarningContains("permissions bindings are not available"), "the missing permissions are logged");
	Update(1000);
	Check(g_get_cookie_calls.empty(), "without the permissions bindings the reconcile reads nothing");
	ToggleFromMenu(0, 0);
	Check(g_players[0].disableSpeed, "the menu still toggles without the cookie store");
	Check(g_set_cookie_calls.empty(), "no cookie is saved without the permissions bindings");
	Check(!g_printed.empty(), "the toggle reply still prints without the cookie store");
	if (DisplayedMenu(0) == 1) {
		ExitMenu(0);
		Update(0);
	}
	End();

	// A rejected OnUserLoaded registration is logged; the reconcile keeps working.
	InstallFakes();
	g_user_loaded_register_status = permissions::Status::CallbackAlreadyExist;
	StartOnMap();
	AddPlayer(0, 111);
	Check(!g_user_loaded_registered, "a rejected listener registration is not remembered");
	Check(AnyWarningContains("OnUserLoaded listener failed"), "the rejected registration is logged");
	g_cookies["111:roundend_disable_speed"] = plg::any(static_cast<int64_t>(1));
	Update(1000);
	Check(g_players[0].disableSpeed, "the reconcile still loads the cookies");
	End();
}

void TestAbsentBindings() {
	Section("absent bindings (fail soft)");
	// Without HookEvent nothing is hooked, the plugin stays Ok and teardown stays quiet.
	InstallFakes();
	__s2sdk_HookEvent = nullptr;
	Start();
	Check(g_hooks.empty(), "without HookEvent nothing is hooked");
	Check(AnyErrorContains("event bindings are unavailable"), "the missing HookEvent is logged");
	End();
	Check(g_unhook_calls.empty(), "nothing is unhooked when nothing was hooked");

	// Without the client accessors round_end does nothing and does not crash.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_IsClientConnected = nullptr;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "without IsClientConnected no field is written");
	Check(!g_server_commands.empty(), "the convar commands still run");
	End();

	// Without IsClientAlive the write loop skips every slot.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_IsClientAlive = nullptr;
	(void)FireEvent("round_end");
	Check(g_float_writes.empty(), "without IsClientAlive no field is written");
	End();

	// Without ServerCommand the convars are skipped but the fields are written.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_ServerCommand = nullptr;
	(void)FireEvent("round_end");
	Check(g_server_commands.empty(), "without ServerCommand no convar command is sent");
	Check(!g_float_writes.empty(), "the field writes still run");
	End();

	// Without PrintToChat the toggle still works (the reply is only lost).
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	Update(1000);
	__s2sdk_PrintToChat = nullptr;
	ToggleFromMenu(0, 0);
	Check(g_players[0].disableSpeed, "the toggle works without PrintToChat");
	Check(g_printed.empty(), "no chat line is printed without the binding");
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 2, "the cookie is still saved");
	if (DisplayedMenu(0) == 1) {
		ExitMenu(0);
		Update(0);
	}
	End();

	// GetMenuItemInfoText is not needed: the handler resolves the picked item from the menu's own
	// item map, so a host without that accessor still gets a working menu.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_GetMenuItemInfoText = nullptr;
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 1, "the menu works without GetMenuItemInfoText");
	SelectMenu(0, 0);
	Update(0);
	Check(g_players[0].disableSpeed, "the selection is still resolved without GetMenuItemInfoText");
	if (DisplayedMenu(0) == 1) {
		ExitMenu(0);
		Update(0);
	}
	End();

	// Without the permissions bindings nothing is registered and teardown is quiet.
	InstallFakes();
	__permissions_OnUserLoaded_Register = nullptr;
	Start();
	Check(!g_user_loaded_registered, "without the permissions bindings no listener is registered");
	End();
	Check(!g_user_loaded_unregistered, "nothing is unregistered when nothing was registered");

	// A throwing registration of the map listeners leaves the plugin Ok.
	InstallFakes();
	__s2sdk_OnMapStart_Register = nullptr;
	__s2sdk_OnMapEnd_Register = nullptr;
	Start();
	Check(!g_map_start_registered && !g_map_end_registered, "no map listener is registered");
	End();
}

void TestReload() {
	Section("reload teardown (A7)");
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 1, "a menu is open before the reload");

	const HookRecord roundEndHook = *FindHook("round_end");
	const HookRecord roundStartHook = *FindHook("round_start");

	End();
	Check(g_hooks.empty(), "both event hooks are removed");
	CheckEqInt(static_cast<long long>(g_unhook_calls.size()), 2, "two UnhookEvent calls");
	bool roundEndUnhooked = false;
	bool roundStartUnhooked = false;
	for (const HookRecord& hook : g_unhook_calls) {
		if (hook.name == "round_end" && hook.callback == roundEndHook.callback &&
			hook.mode == s2sdk::HookMode::Pre) {
			roundEndUnhooked = true;
		}
		if (hook.name == "round_start" && hook.callback == roundStartHook.callback &&
			hook.mode == s2sdk::HookMode::Pre) {
			roundStartUnhooked = true;
		}
	}
	Check(roundEndUnhooked, "round_end is unhooked with the same callback and Pre mode");
	Check(roundStartUnhooked, "round_start is unhooked with the same callback and Pre mode");
	Check(g_map_start_unregistered && g_map_end_unregistered, "both map listeners are unregistered");
	Check(!g_mapGate.Ready() && !g_mapGate.ListenersRegistered(), "the gate is closed after the unload");
	CheckEqInt(static_cast<long long>(g_remove_command_calls.size()), 2, "both owned commands are removed");
	Check(g_user_loaded_unregistered, "the permissions listener is unregistered");
	Check(g_menus.empty(), "every menu (and the reserved dummy) is destroyed");
	Check(!g_reservedMenuIdZero, "the reserved MenuId 0 is released");
	CheckEqInt(g_backend_close_calls, 0, "the checked teardown never calls the backend");
	CheckEqInt(g_velocityOffset, -1, "the schema offset cache is dropped");
	CheckEqInt(g_gravityOffset, -1, "the gravity offset cache is dropped");
	CheckEqInt(g_actualGravityOffset, -1, "the actual gravity offset cache is dropped");

	// A stale callback fired after the unload touches no client (the gate is shut).
	g_server_commands.clear();
	g_client_calls.clear();
	g_float_writes.clear();
	g_schema_lookups.clear();
	(void)roundEndHook.callback(plg::string("round_end"), nullptr, false);
	Check(g_client_calls.empty(), "a stale round_end callback makes no client call");
	Check(g_float_writes.empty(), "a stale round_end callback makes no schema write");
	Check(g_server_commands.empty(), "a stale round_end callback sends no convar command");
	Update(0);
	Check(g_client_calls.empty(), "an update after the unload makes no client call");

	// Starting again registers everything exactly once again.
	g_hook_calls.clear();
	g_command_calls.clear();
	g_remove_command_calls.clear();
	g_remove_listener_calls.clear();
	g_map_start_registered = false;
	g_map_end_registered = false;
	g_map_start_unregistered = false;
	g_map_end_unregistered = false;
	g_user_loaded_registered = false;
	g_user_loaded_unregistered = false;
	Start();
	CheckEqInt(static_cast<long long>(g_hook_calls.size()), 2, "a reload hooks the two events once");
	CheckEqInt(static_cast<long long>(g_command_calls.size()), 2, "a reload registers the two commands once");
	Check(g_map_start_registered && g_map_end_registered, "a reload registers the map listeners once");
	Check(g_user_loaded_registered, "a reload registers the permissions listener once");
	CheckEqInt(static_cast<long long>(g_menus.size()), 0,
			   "the reload burns a fresh id without leaking a dummy (id 0 is already consumed)");
	FireMapStart();
	Update(0);
	Check(g_mapGate.Ready(), "the reloaded plugin works on the map");
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 1, "the reloaded plugin opens the menu");
	Check(g_client_menu[0].has_value() && *g_client_menu[0] != 0, "the reloaded menu has a usable id");
	End();
	Check(g_menus.empty(), "the reloaded plugin frees its menus on unload");
	CheckEqInt(g_timer_calls, 0, "no s2sdk timer was ever created");
}

void TestMenuTypes() {
	Section("menu type: hudmenu per-player choice (pisexcompat resolve)");
	// The player's own "!menu" choice wins over the default HUD_LAYOUT mapping.
	const std::pair<const char*, const char*> choices[] = {
			{"chat", "chat"}, {"centerhtml", "centerhtml"}, {"button", "button"},
			{"hud", "hud"},   {"wasd", "wasd"},
	};
	for (const auto& [choice, expected] : choices) {
		InstallFakes();
		g_menu_types.insert("hud");
		g_menu_types.insert("wasd");
		g_hudmenu_choice[0] = choice;
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
			CheckEqStr(menu->type, expected,
					   std::string("the player's !menu choice \"") + choice + "\" is used");
		} else {
			Check(false, std::string("no menu for the hubmenu choice ") + choice);
		}
		End();
	}

	// A "hud"/"wasd" choice whose backend is not registered falls back to the on-screen "button"
	// (pisexcompat ResolveS2sdkMenuTypeName does exactly that).
	for (const char* choice : {"hud", "wasd"}) {
		InstallFakes();
		g_hudmenu_choice[0] = choice;
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
			CheckEqStr(menu->type, "button",
					   std::string("the down \"") + choice + "\" backend falls back to button");
		} else {
			Check(false, std::string("no menu for the down backend ") + choice);
		}
		End();
	}

	// An unknown hudmenu answer falls back to the default: "hud" when registered, else "button".
	{
		InstallFakes();
		g_menu_types.insert("hud");
		g_hudmenu_choice[0] = "unknown-type";
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
			CheckEqStr(menu->type, "hud", "an unknown choice keeps the hud default");
		}
		End();
	}
	{
		InstallFakes();
		g_hudmenu_choice[0] = "unknown-type";
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
			CheckEqStr(menu->type, "button", "an unknown choice without hud falls back to button");
		}
		End();
	}

	Check(g_hudmenu_calls > 0, "hudmenu's GetClientMenuType is asked when a menu is opened");

	// hudmenu installed but no per-player answer (empty string): the default HUD_LAYOUT mapping.
	{
		InstallFakes();
		g_menu_types.insert("hud");
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
			CheckEqStr(menu->type, "hud", "no player choice: hud is the default when registered");
		}
		End();
	}

	// No hudmenu plugin at all (binding null): the default path still works.
	{
		InstallFakes();
		g_menu_types.insert("hud");
		__hudmenu_GetClientMenuType = nullptr;
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
			CheckEqStr(menu->type, "hud", "without hudmenu the default is still hud");
		}
		End();

		InstallFakes();
		__hudmenu_GetClientMenuType = nullptr;
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		if (const FakeMenu* menu = DisplayedMenuOf(0); menu != nullptr) {
			CheckEqStr(menu->type, "button", "without hudmenu and without the hud backend: button");
		}
		End();
	}
}

void TestMenuCleanup() {
	Section("menu teardown on map change / unload (C10)");
	// An active display at the map end: the session is ended with the Disconnect reason, so the
	// backend's close callback (a client call) is never invoked while the map is going down; the
	// menu instance is then destroyed with no viewer left.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	const s2sdk::MenuId liveId = *g_client_menu[0];
	Check(HasClientMenu(0), "the menu is displayed before the map end");
	FireMapEnd();
	CheckEqInt(g_backend_close_calls, 0, "the map-end teardown makes no backend close call");
	Check(!HasClientMenu(0), "the display session is ended at the map end");
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "only the reserved dummy is left after the map end");
	Check(g_menus.count(liveId) == 0, "the plugin's menu instance is destroyed");
	CheckEqInt(g_destroy_calls_in_handler, 0, "the map-end destroy runs outside the handler");
	End();
	CheckEqInt(g_backend_close_calls, 0, "the unload between maps makes no backend close call");
	Check(g_menus.empty(), "the reserved dummy is destroyed on the unload");
	Check(!g_reservedMenuIdZero, "the reserved id is released");

	// A menu displayed while the plugin unloads on a live map is released through the same checked
	// path: the display is ended with the Disconnect reason first, so the backend is not called even
	// though a live client exists, and the reserved dummy is destroyed as well.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	End();
	Check(g_menus.empty(), "the live-map unload destroys every menu instance");
	CheckEqInt(g_backend_close_calls, 0, "the checked teardown never calls the backend, map or not");

	// A second open whose display is refused leaves the previous menu queued *while it is still
	// displayed*: the map-end drain has to end that session with the Disconnect reason before the
	// destroy, so the backend's close callback is still never called while the map is down.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	Check(HasClientMenu(0), "the first menu is displayed");
	g_menu_types.clear(); // every DisplayMenu call now fails after the old menu was queued
	OpenViaConsoleMenu(0);
	Check(HasClientMenu(0), "the refused replacement leaves the old display open");
	CheckEqInt(g_backend_close_calls, 0, "the refused replacement makes no backend call");
	FireMapEnd();
	CheckEqInt(g_backend_close_calls, 0, "the map end ends the still-displayed session without a close");
	Check(!HasClientMenu(0), "the still-displayed session is cleared at the map end");
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "only the reserved dummy survives");
	End();
	Check(g_menus.empty(), "nothing leaks on the unload");

	// A menu whose display already ended is destroyed by the update without any backend call.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	ExitMenu(0); // the manager ends the display; our End handler queues the destroy
	CheckEqInt(g_backend_close_calls, 0, "the user's exit is the engine's close, not the plugin's teardown");
	Update(0);
	CheckEqInt(g_backend_close_calls, 0, "destroying a closed menu makes no backend call");
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "only the reserved dummy is left");
	End();

	// The map-down fail-safe graffiti established: CancelClientMenu answers false while the display
	// is still active (an s2sdk MenuManager contract violation). The menu is switched to the builtin
	// no-op-close "chat" type before DestroyMenu, so the backend is never touched on the torn-down
	// client layer and no handler of this library survives the unload.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	const s2sdk::MenuId failSafeId = *g_client_menu[0];
	Check(HasClientMenu(0), "the menu is displayed before the contract violation");
	g_cancel_fails = true; // CancelClientMenu now answers false and leaves the display active
	FireMapEnd();
	CheckEqInt(g_backend_close_calls, 0, "the fail-safe makes no backend close call");
	Check(!HasClientMenu(0), "the fail-safe clears the display");
	CheckEqInt(static_cast<long long>(g_type_switches.size()), 1,
			   "the fail-safe switches the menu to a no-op-close builtin type");
	if (!g_type_switches.empty()) {
		CheckEqStr(g_type_switches[0], "chat", "the builtin chat close is the no-op one");
	}
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "the plugin's menu is destroyed, the dummy stays");
	Check(g_menus.count(failSafeId) == 0, "the fail-safe menu instance is gone");
	Check(g_printed.empty() && g_set_cookie_calls.empty(),
		  "the map-down fail-safe produces no chat/cookie callback");
	CheckEqInt(g_destroy_calls_in_handler, 0, "the fail-safe destroy runs outside the handler");
	End();
	CheckEqInt(g_backend_close_calls, 0, "the unload after the fail-safe makes no backend close either");
	Check(g_menus.empty(), "nothing leaks on the unload after the fail-safe");

	// If even the no-op type switch is impossible, the menu cannot be released safely: the request
	// is retried and, at the unload, reported as A7_UNRESOLVED instead of being silently dropped.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	const s2sdk::MenuId unresolvedId = *g_client_menu[0];
	g_cancel_fails = true;
	g_set_menu_type_fails = true;
	FireMapEnd();
	CheckEqInt(g_backend_close_calls, 0, "no backend close is attempted when even the switch fails");
	Check(AnyWarningContains("still displayed"), "the unresolved destroy is retried and logged");
	Check(HasClientMenu(0), "the display is left untouched rather than destroyed unsafely");
	End();
	Check(AnyErrorContains("A7_UNRESOLVED"), "the unresolved menu is reported as A7_UNRESOLVED");
	Check(g_menus.count(unresolvedId) == 1, "the unresolved menu is not silently dropped");
	Check(g_menus.count(0) == 0, "the reserved id is still released");
	CheckEqInt(g_backend_close_calls, 0, "the unresolved path never calls the backend");

	// A partially available menu binding set never creates a menu: without the cleanup half (and the
	// type API the map-down fail-safe needs) a display could not be ended safely at a map change.
	for (const char* missing : {"GetClientMenu", "CancelClientMenu", "SetMenuType", "GetMenuType"}) {
		InstallFakes();
		if (std::string_view(missing) == "GetClientMenu") {
			__s2sdk_GetClientMenu = nullptr;
		} else if (std::string_view(missing) == "CancelClientMenu") {
			__s2sdk_CancelClientMenu = nullptr;
		} else if (std::string_view(missing) == "SetMenuType") {
			__s2sdk_SetMenuType = nullptr;
		} else {
			__s2sdk_GetMenuType = nullptr;
		}
		StartOnMap();
		AddPlayer(0, 111);
		OpenViaConsoleMenu(0);
		CheckEqInt(DisplayedMenu(0), 0, std::string("no menu without ") + missing);
		CheckEqInt(static_cast<long long>(g_menus.size()), 0,
				   std::string("no reserved dummy without ") + missing);
		End();
		Check(g_menus.empty(), "nothing leaks on the unload");
	}
}

void TestMenuIdentity() {
	Section("menu identity: nonzero SteamID64 + exact controller serial");
	// A missing controller accessor fails soft: no menu is created at all (and there is nothing to
	// re-check later).
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_PlayerSlotToEntHandle = nullptr;
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 0, "no menu without PlayerSlotToEntHandle");
	Check(AnyWarningContains("identity"), "the missing controller accessor is logged");
	CheckEqInt(static_cast<long long>(g_menus.size()), 1, "only the reserved dummy exists");
	End();

	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_IsValidEntHandle = nullptr;
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 0, "no menu without IsValidEntHandle");
	Check(AnyWarningContains("identity"), "the missing handle validation is logged");
	End();

	// A missing SteamID64 accessor and a player without a SteamID64 fail soft as well.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	__s2sdk_GetClientSteamID64 = nullptr;
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 0, "no menu without GetClientSteamID64");
	End();

	InstallFakes();
	StartOnMap();
	AddPlayer(0, 0); // in game, but the engine answers SteamID64 == 0
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 0, "no menu for a slot without a nonzero SteamID64");
	Check(AnyWarningContains("identity"), "the missing SteamID64 is logged");
	End();

	// 0 and -1 are the invalid controller handle values: no menu.
	for (const int32_t invalid : {0, -1}) {
		InstallFakes();
		StartOnMap();
		AddPlayer(0, 111);
		g_fake.slots[0].controller_handle = invalid;
		OpenViaConsoleMenu(0);
		CheckEqInt(DisplayedMenu(0), 0,
				   std::string("no menu for the invalid controller handle ") + std::to_string(invalid));
		End();
	}

	// A valid *negative* controller handle (serial into bit 31) is accepted end-to-end: the menu
	// opens and a selection is applied for the exact same handle.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	g_fake.slots[0].controller_handle = -4242;
	OpenViaConsoleMenu(0);
	CheckEqInt(DisplayedMenu(0), 1, "a valid negative controller handle opens the menu");
	SelectMenu(0, 0);
	Update(0);
	Check(g_players[0].disableSpeed, "the selection applies with a valid negative serial handle");
	Check(!g_printed.empty() && g_printed.back().message == "Настройка скорости изменена.",
		  "the reply is printed for the valid negative handle");
	CheckEqInt(static_cast<long long>(g_set_cookie_calls.size()), 2,
			   "the cookies are saved for the valid negative handle");
	End();

	// The same SteamID64 reconnecting with a new controller entity (new serial) is a different
	// owner: the recorded selection is dropped with no toggle, chat line, cookie or re-open.
	InstallFakes();
	StartOnMap();
	AddPlayer(0, 111);
	OpenViaConsoleMenu(0);
	SelectMenu(0, 0);
	AddPlayer(0, 111); // same account, new controller entity/serial (generation++)
	Update(0);
	Check(!g_players[0].disableSpeed, "a new controller serial drops the same-account selection");
	Check(g_printed.empty(), "no chat line for the same account with a new controller");
	Check(g_set_cookie_calls.empty(), "no cookie for the same account with a new controller");
	CheckEqInt(DisplayedMenu(0), 0, "the menu is not re-opened for the new controller entity");
	End();
}

void TestPairedBindings() {
	Section("paired teardown bindings (A7)");
	// Commands: without either remover no command is registered at all. The ownership of the name is
	// only known after AddConsoleCommand ("created here" vs "existing entry with our listener"), so
	// both removers are part of the required pair.
	for (const char* missing : {"RemoveCommand", "RemoveCommandListener"}) {
		InstallFakes();
		WriteConfig(kEnabledConfig);
		if (std::string_view(missing) == "RemoveCommand") {
			__s2sdk_RemoveCommand = nullptr;
		} else {
			__s2sdk_RemoveCommandListener = nullptr;
		}
		Start();
		Check(g_command_calls.empty(), std::string("no command is registered without ") + missing);
		Check(AnyErrorContains("command bindings are unavailable"),
			  std::string("the missing ") + missing + " is logged");
		End();
		Check(g_remove_command_calls.empty() && g_remove_listener_calls.empty(),
			  "nothing is left to remove when nothing was registered");
	}

	// Events: without UnhookEvent neither event is hooked.
	InstallFakes();
	WriteConfig(kEnabledConfig);
	__s2sdk_UnhookEvent = nullptr;
	Start();
	Check(g_hook_calls.empty(), "no event is hooked without UnhookEvent");
	Check(AnyErrorContains("event bindings are unavailable"), "the missing UnhookEvent is logged");
	End();
	Check(g_unhook_calls.empty(), "nothing is left to unhook");

	// Map listeners: any missing endpoint (both registrations and both unregistrations are required)
	// disables the registration of both listeners and keeps the gate shut.
	for (const char* missing : {"OnMapStart_Register", "OnMapEnd_Register", "OnMapStart_Unregister",
								"OnMapEnd_Unregister"}) {
		InstallFakes();
		WriteConfig(kEnabledConfig);
		if (std::string_view(missing) == "OnMapStart_Register") {
			__s2sdk_OnMapStart_Register = nullptr;
		} else if (std::string_view(missing) == "OnMapEnd_Register") {
			__s2sdk_OnMapEnd_Register = nullptr;
		} else if (std::string_view(missing) == "OnMapStart_Unregister") {
			__s2sdk_OnMapStart_Unregister = nullptr;
		} else {
			__s2sdk_OnMapEnd_Unregister = nullptr;
		}
		Start();
		Check(!g_map_start_registered && !g_map_end_registered,
			  std::string("no map listener is registered without ") + missing);
		Check(AnyErrorContains("map listeners are unavailable"),
			  std::string("the missing ") + missing + " is logged");
		AddPlayer(0, 111);
		Update(1000);
		(void)FireEvent("round_end");
		Check(!g_mapGate.Ready(), "the gate stays shut");
		Check(g_client_calls.empty(), "no client call is made");
		End();
	}

	// The permissions pair was already validated as a whole: a missing unregister disables the
	// listener registration too.
	InstallFakes();
	WriteConfig(kEnabledConfig);
	__permissions_OnUserLoaded_Unregister = nullptr;
	Start();
	Check(!g_user_loaded_registered, "no permissions listener without its unregister");
	Check(AnyWarningContains("permissions bindings are not available"), "the missing permissions pair is logged");
	End();
}

void TestNoPlayerTypedFields() {
	Section("player-typed event accessors are not needed");
	InstallFakes();
	__s2sdk_GetEventPlayerSlot = nullptr;
	__s2sdk_GetEventInt = nullptr;
	StartOnMap();
	AddPlayer(0, 111);
	(void)FireEvent("round_end");
	Check(!g_float_writes.empty(), "round_end works with both accessors absent");
	g_float_writes.clear();
	(void)FireEvent("round_start");
	Check(!g_float_writes.empty(), "round_start works with both accessors absent");
	End();
}

} // namespace test

int main() {
	test::TestStartup();
	test::TestMapGate();
	test::TestRoundEnd();
	test::TestSchemaFailures();
	test::TestConfig();
	test::TestCommands();
	test::TestMenuToggles();
	test::TestPermissions();
	test::TestMenuTypes();
	test::TestMenuCleanup();
	test::TestMenuIdentity();
	test::TestAbsentBindings();
	test::TestPairedBindings();
	test::TestReload();
	test::TestNoPlayerTypedFields();
	test::CleanupTempDirs();
	std::printf("%d checks, %d failures\n", test::g_checks, test::g_failures);
	return test::g_failures == 0 ? 0 : 1;
}
