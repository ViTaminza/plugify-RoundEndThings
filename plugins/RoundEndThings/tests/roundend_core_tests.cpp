// RoundEndThings core tests — the engine-independent part of the pure-s2sdk port
// (docs/PLUGIN_STANDARD.md §7: no pisexcompat, no guard library).
//
// Covered:
//   * the original's menu labels ("Отключить скорость [X]" / "[ ]", "Отключить гравитацию ..."),
//     which are literals in RoundEndThings.cpp (the original has no phrase file);
//   * the cookie value readers of roundend_disable_speed / roundend_disable_gravity: every numeric
//     shape the permissions store may hold (this plugin writes int64 0/1; the Metamod-era Pisex
//     cookie store held the strings "0"/"1") and the rejection of unusable shapes, so the caller
//     keeps the default;
//   * the settings.ini defaults of the original and its KeyValues GetBool/GetFloat semantics
//     (the pisexcompat behaviour the HEAD port had: "true"/"yes"/"on", atoi(), strtof), verified
//     against the production settings.ini from tests/data;
//   * the exact pisexcompat candidate order of the unchanged original path
//     "addons/configs/RoundEndThings/settings.ini" (raw cwd first, then configs/translations/data)
//     and the first-existing rule (a malformed primary file keeps the defaults, it never falls
//     through to a valid lower-priority copy);
//   * the menu callback decision (info string -> toggle + exact reply).
//
// No engine, no s2sdk, no Plugify host. Run by ../../test.sh (build/tests/roundend_core_tests).

#include "roundend_core.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace {

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
	if (std::fabs(actual - expected) > 1e-6f) {
		++g_failures;
		std::printf("FAIL: %s (expected %g, got %g)\n", what.c_str(), expected, actual);
	}
}

// --- temp files -----------------------------------------------------------------------------------

class TempTree {
public:
	TempTree() {
		static int counter = 0;
		m_root = std::filesystem::temp_directory_path() /
				 ("roundend_core_tests_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
		std::filesystem::create_directories(m_root);
	}
	~TempTree() {
		std::error_code error;
		std::filesystem::remove_all(m_root, error);
	}
	TempTree(const TempTree&) = delete;
	TempTree& operator=(const TempTree&) = delete;

	[[nodiscard]] std::filesystem::path Root() const { return m_root; }

	std::string Write(const std::string& relative, std::string_view content) const {
		const std::filesystem::path path = m_root / relative;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << content;
		file.close();
		return path.string();
	}

private:
	std::filesystem::path m_root;
};

// RAII chdir for the raw cwd candidate.
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

std::string TestDataPath(const std::string& relative) {
	return std::string(ROUNDEND_TEST_DATA_DIR) + "/" + relative;
}

// --- tests ----------------------------------------------------------------------------------------

void TestLabels() {
	Section("menu labels");
	roundend::PlayerSettings settings;
	CheckEqStr(roundend::SpeedMenuLabel(settings), "Отключить скорость [ ]", "speed label, not disabled");
	CheckEqStr(roundend::GravityMenuLabel(settings), "Отключить гравитацию [ ]", "gravity label, not disabled");

	settings.disableSpeed = true;
	CheckEqStr(roundend::SpeedMenuLabel(settings), "Отключить скорость [X]", "speed label, disabled");
	settings.disableGravity = true;
	CheckEqStr(roundend::GravityMenuLabel(settings), "Отключить гравитацию [X]", "gravity label, disabled");

	// The title is the original's literal, unchanged.
	CheckEqStr(roundend::kMenuTitle, "Настройки конца раунда", "menu title");
	CheckEqStr(roundend::kSpeedChanged, "Настройка скорости изменена.", "speed reply");
	CheckEqStr(roundend::kGravityChanged, "Настройка гравитации изменена.", "gravity reply");
}

void TestCookieValues() {
	Section("cookie values");
	bool out = false;

	Check(roundend::CookieValueToBool(plg::any(static_cast<int64_t>(0)), out) && !out, "int64 0 -> false");
	Check(roundend::CookieValueToBool(plg::any(static_cast<int64_t>(1)), out) && out, "int64 1 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<int64_t>(-3)), out) && out, "int64 -3 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<int32_t>(2)), out) && out, "int32 2 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<int16_t>(0)), out) && !out, "int16 0 -> false");
	Check(roundend::CookieValueToBool(plg::any(static_cast<int8_t>(1)), out) && out, "int8 1 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<uint64_t>(0)), out) && !out, "uint64 0 -> false");
	Check(roundend::CookieValueToBool(plg::any(static_cast<uint32_t>(7)), out) && out, "uint32 7 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<uint16_t>(1)), out) && out, "uint16 1 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<uint8_t>(1)), out) && out, "uint8 1 -> true");
	Check(roundend::CookieValueToBool(plg::any(true), out) && out, "bool true -> true");
	Check(roundend::CookieValueToBool(plg::any(false), out) && !out, "bool false -> false");

	// The Metamod-era Pisex cookie store wrote the strings "0"/"1" (the original ran atoi()).
	Check(roundend::CookieValueToBool(plg::any(plg::string("0")), out) && !out, "string \"0\" -> false");
	Check(roundend::CookieValueToBool(plg::any(plg::string("1")), out) && out, "string \"1\" -> true");
	Check(roundend::CookieValueToBool(plg::any(plg::string("3")), out) && out, "string \"3\" -> true");

	// Unusable shapes are reported, so the caller keeps the default.
	out = true;
	Check(!roundend::CookieValueToBool(plg::any(plg::string("yes")), out), "non-numeric string is rejected");
	Check(!roundend::CookieValueToBool(plg::any(plg::string()), out), "empty string is rejected");
	Check(!roundend::CookieValueToBool(plg::any(1.0f), out), "float is rejected (the store writes integers)");
	Check(!roundend::CookieValueToBool(plg::any(), out), "empty any is rejected");
}

void TestDefaults() {
	Section("settings defaults");
	const roundend::Settings defaults;
	Check(defaults.bhopEnabled && defaults.speedEnabled && defaults.gravityEnabled,
		  "the built-in defaults enable all three features");
	CheckNear(defaults.speed, 1.5f, "default speed");
	CheckNear(defaults.gravity, 1.5f, "default gravity");
	CheckNear(roundend::kDefaultSpeed, 1.0f, "DEFAULT_SPEED");
	CheckNear(roundend::kDefaultGravity, 1.0f, "DEFAULT_GRAVITY");
}

void TestProductionFile() {
	Section("production settings.ini");
	// The production file shipped in tests/data, read with the plugin-local KV1 reader.
	const std::optional<roundend::KvNode> root =
			roundend::LoadKv1File(TestDataPath("RoundEndThings/settings.ini"));
	Check(root.has_value(), "the production settings.ini parses");
	if (!root) {
		return;
	}
	const roundend::Settings settings = roundend::ParseSettings(*root);
	Check(settings.bhopEnabled, "production: bhop_enabled 1 -> true");
	Check(!settings.speedEnabled, "production: speed_enabled 0 -> false");
	Check(!settings.gravityEnabled, "production: gravity_enabled 0 -> false");
	CheckNear(settings.speed, 1.0f, "production: speed 1.0");
	CheckNear(settings.gravity, 1.0f, "production: gravity 1.0");
}

void TestKvSemantics() {
	Section("KeyValues bool/float semantics of the HEAD port");
	TempTree tree;

	// A missing key keeps the default; the defaults match the original's LoadConfig arguments.
	{
		const std::optional<roundend::KvNode> root = roundend::ParseKv1("\"Config\"\n{\n}\n");
		Check(root.has_value(), "an empty Config block parses");
		if (root) {
			const roundend::Settings settings = roundend::ParseSettings(*root);
			Check(settings.bhopEnabled && settings.speedEnabled && settings.gravityEnabled,
				  "missing booleans keep true");
			CheckNear(settings.speed, 1.5f, "missing speed keeps 1.5");
			CheckNear(settings.gravity, 1.5f, "missing gravity keeps 1.5");
		}
	}

	// pisexcompat's KeyValues::GetBool: "true"/"yes"/"on" and "false"/"no"/"off" are spelled out,
	// everything else goes through atoi() ("abc" -> false), an empty value keeps the default.
	{
		const std::optional<roundend::KvNode> root = roundend::ParseKv1(
				"\"Config\"\n{\n"
				"    \"bhop_enabled\" \"true\"\n"
				"    \"speed_enabled\" \"off\"\n"
				"    \"gravity_enabled\" \"abc\"\n"
				"    \"speed\" \"2\"\n"
				"    \"gravity\" \"0.5\"\n"
				"}\n");
		Check(root.has_value(), "the bool/float file parses");
		if (root) {
			const roundend::Settings settings = roundend::ParseSettings(*root);
			Check(settings.bhopEnabled, "\"true\" -> true");
			Check(!settings.speedEnabled, "\"off\" -> false");
			Check(!settings.gravityEnabled, "\"abc\" -> false (atoi)");
			CheckNear(settings.speed, 2.0f, "\"2\" -> 2.0");
			CheckNear(settings.gravity, 0.5f, "\"0.5\" -> 0.5");
		}
	}
	{
		const std::optional<roundend::KvNode> root = roundend::ParseKv1(
				"\"Config\"\n{\n"
				"    \"bhop_enabled\" \"\"\n"
				"    \"speed\" \"\"\n"
				"}\n");
		Check(root.has_value(), "the empty-value file parses");
		if (root) {
			const roundend::Settings settings = roundend::ParseSettings(*root);
			Check(settings.bhopEnabled, "an empty bool value keeps the default");
			CheckNear(settings.speed, 1.5f, "an empty float value keeps the default (pisexcompat)");
		}
	}
	{
		// Key lookup is case-insensitive, like Valve's FindKey.
		const std::optional<roundend::KvNode> root =
				roundend::ParseKv1("\"Config\"\n{\n    \"BHOP_ENABLED\" \"0\"\n}\n");
		Check(root.has_value(), "the mixed-case file parses");
		if (root) {
			Check(!roundend::ParseSettings(*root).bhopEnabled, "key lookup is case-insensitive");
		}
	}

	// The plugin-local reader rejects a malformed document as a whole.
	const std::string path = tree.Write("malformed/settings.ini", "\"Config\"\n{\n    \"speed\" \"unterminated\n");
	const std::optional<roundend::KvNode> malformed = roundend::LoadKv1File(path);
	Check(!malformed.has_value(), "an unterminated string makes the file invalid");
}

void TestChoice() {
	Section("menu callback decision");
	roundend::PlayerSettings settings;

	Check(roundend::ChoiceFromInfo("speed") == roundend::MenuChoice::Speed, "\"speed\" is the speed item");
	Check(roundend::ChoiceFromInfo("gravity") == roundend::MenuChoice::Gravity,
		  "\"gravity\" is the gravity item");
	Check(roundend::ChoiceFromInfo("") == roundend::MenuChoice::None, "an empty info is no choice");
	Check(roundend::ChoiceFromInfo("Speed") == roundend::MenuChoice::None,
		  "the original compared case-sensitively");

	CheckEqStr(roundend::ApplyChoice(settings, roundend::MenuChoice::Speed),
			   "Настройка скорости изменена.", "speed reply");
	Check(settings.disableSpeed && !settings.disableGravity, "the speed toggle flips only speed");
	CheckEqStr(roundend::ApplyChoice(settings, roundend::MenuChoice::Gravity),
			   "Настройка гравитации изменена.", "gravity reply");
	Check(settings.disableSpeed && settings.disableGravity, "the gravity toggle flips only gravity");
	Check(roundend::ApplyChoice(settings, roundend::MenuChoice::Speed) != nullptr, "the speed toggle is repeatable");
	Check(!settings.disableSpeed, "the second speed selection flips it back");
	Check(roundend::ApplyChoice(settings, roundend::MenuChoice::None) == nullptr, "no choice flips nothing");

	// The label of the re-opened menu reflects the new state.
	CheckEqStr(roundend::SpeedMenuLabel(settings), "Отключить скорость [ ]", "label after flip back");
	CheckEqStr(roundend::GravityMenuLabel(settings), "Отключить гравитацию [X]", "gravity label stays [X]");
}

void TestCandidates() {
	Section("candidate order");
	const std::vector<std::string> with_hosts = roundend::ConfigCandidates("/cfg", "/data");
	CheckEqInt(static_cast<long long>(with_hosts.size()), 4, "four candidates with a Plugify host");
	if (with_hosts.size() == 4) {
		CheckEqStr(with_hosts[0], "addons/configs/RoundEndThings/settings.ini", "raw cwd path is first");
		CheckEqStr(with_hosts[1], "/cfg/RoundEndThings/settings.ini", "plugin config dir is second");
		CheckEqStr(with_hosts[2], "/cfg/RoundEndThings/translations/settings.ini",
				   "plugin translations dir is third");
		CheckEqStr(with_hosts[3], "/data/RoundEndThings/settings.ini", "plugin data dir is fourth");
	}

	// Without GetConfigsDir/GetDataDir pisexcompat built JoinPath("", name): the relative plugin
	// spelling, and the raw cwd path is still first.
	const std::vector<std::string> no_hosts = roundend::ConfigCandidates("", "");
	CheckEqInt(static_cast<long long>(no_hosts.size()), 4, "four candidates without a host");
	if (no_hosts.size() == 4) {
		CheckEqStr(no_hosts[0], "addons/configs/RoundEndThings/settings.ini", "raw cwd path still first");
		CheckEqStr(no_hosts[1], "RoundEndThings/settings.ini", "relative plugin config dir");
		CheckEqStr(no_hosts[2], "RoundEndThings/translations/settings.ini", "relative translations dir");
		CheckEqStr(no_hosts[3], "RoundEndThings/settings.ini", "relative data dir spelling");
	}

	// Trailing slashes are not doubled (JoinPath of pisexcompat).
	const std::vector<std::string> trailing = roundend::ConfigCandidates("/cfg/", "/data/");
	CheckEqStr(trailing[1], "/cfg/RoundEndThings/settings.ini", "no doubled slash in the config dir");

	CheckEqStr(roundend::JoinPath("", "x.ini"), "x.ini", "JoinPath: empty left");
	CheckEqStr(roundend::JoinPath("a", ""), "a", "JoinPath: empty right");
	CheckEqStr(roundend::JoinPath("a/", "/b"), "a/b", "JoinPath: one slash");
}

void TestLoadSettings() {
	Section("first-existing config");
	TempTree tree;

	// 1. The raw cwd file wins over every configs copy.
	{
		tree.Write("cwd/addons/configs/RoundEndThings/settings.ini",
				   "\"Config\"\n{\n    \"speed\" \"2.5\"\n    \"speed_enabled\" \"1\"\n}\n");
		tree.Write("configs/RoundEndThings/settings.ini", "\"Config\"\n{\n    \"speed\" \"9.0\"\n}\n");
		const ScopedCwd cwd(tree.Root() / "cwd");
		const roundend::ConfigLoadResult result = roundend::LoadSettings(
				roundend::ConfigCandidates(tree.Root().string() + "/configs", tree.Root().string() + "/data"));
		Check(result.found && result.parsed, "the raw cwd file is found and parsed");
		CheckNear(result.settings.speed, 2.5f, "the raw cwd settings.ini wins (speed)");
		Check(result.settings.speedEnabled, "the raw cwd settings.ini wins (speed_enabled)");
		CheckEqStr(result.path, "addons/configs/RoundEndThings/settings.ini", "the reported path is the raw one");
	}

	// 2. Without a raw file the plugin config dir is used.
	{
		tree.Write("configs2/RoundEndThings/settings.ini",
				   "\"Config\"\n{\n    \"bhop_enabled\" \"0\"\n}\n");
		const roundend::ConfigLoadResult result = roundend::LoadSettings(
				roundend::ConfigCandidates(tree.Root().string() + "/configs2", tree.Root().string() + "/data2"));
		Check(result.found && result.parsed, "the configs file is found");
		Check(!result.settings.bhopEnabled, "the configs file is used");
		Check(result.path.find("/configs2/RoundEndThings/settings.ini") != std::string::npos,
			  "the reported path is the configs one");
	}

	// 3. The translations candidate comes before the data candidate.
	{
		tree.Write("configs3/RoundEndThings/translations/settings.ini",
				   "\"Config\"\n{\n    \"gravity\" \"0.25\"\n}\n");
		tree.Write("data3/RoundEndThings/settings.ini", "\"Config\"\n{\n    \"gravity\" \"7.0\"\n}\n");
		const roundend::ConfigLoadResult result = roundend::LoadSettings(
				roundend::ConfigCandidates(tree.Root().string() + "/configs3", tree.Root().string() + "/data3"));
		Check(result.found && result.parsed, "the translations file is found");
		CheckNear(result.settings.gravity, 0.25f, "the translations candidate wins over data");
	}

	// 4. The data candidate is the last fallback.
	{
		tree.Write("data4/RoundEndThings/settings.ini", "\"Config\"\n{\n    \"gravity\" \"4.0\"\n}\n");
		const roundend::ConfigLoadResult result = roundend::LoadSettings(
				roundend::ConfigCandidates(tree.Root().string() + "/configs4", tree.Root().string() + "/data4"));
		Check(result.found && result.parsed, "the data file is found");
		CheckNear(result.settings.gravity, 4.0f, "the data candidate is used");
	}

	// 5. Nothing exists: not found, and the reported path is the most likely configs location.
	{
		const roundend::ConfigLoadResult result = roundend::LoadSettings(
				roundend::ConfigCandidates(tree.Root().string() + "/nope", tree.Root().string() + "/nodata"));
		Check(!result.found, "no candidate -> not found");
		Check(!result.parsed, "no candidate -> not parsed");
		Check(result.path.find("/nope/RoundEndThings/settings.ini") != std::string::npos,
			  "the log path is the plugin config dir");
		CheckNear(result.settings.speed, 1.5f, "not found keeps the built-in defaults");
	}

	// 6. The first existing file is authoritative: a malformed raw file keeps the defaults and does
	//    NOT fall through to a valid configs copy (pisexcompat's FirstExisting + LoadFromFile).
	{
		tree.Write("cwd2/addons/configs/RoundEndThings/settings.ini",
				   "\"Config\"\n{\n    \"speed\" \"unterminated\n");
		tree.Write("configs5/RoundEndThings/settings.ini", "\"Config\"\n{\n    \"speed\" \"8.0\"\n}\n");
		const ScopedCwd cwd(tree.Root() / "cwd2");
		const roundend::ConfigLoadResult result = roundend::LoadSettings(
				roundend::ConfigCandidates(tree.Root().string() + "/configs5", tree.Root().string() + "/data5"));
		Check(result.found, "the malformed raw file is found");
		Check(!result.parsed, "the malformed raw file is not parsed");
		CheckEqStr(result.path, "addons/configs/RoundEndThings/settings.ini", "the malformed raw file is reported");
		CheckNear(result.settings.speed, 1.5f, "a malformed primary file keeps the defaults");
	}
}

} // namespace

int main() {
	TestLabels();
	TestCookieValues();
	TestDefaults();
	TestProductionFile();
	TestKvSemantics();
	TestChoice();
	TestCandidates();
	TestLoadSettings();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
