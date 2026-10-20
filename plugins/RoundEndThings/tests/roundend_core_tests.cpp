// RoundEndThings unit tests — the menu labels and the cookie value readers of the original's
// per-player settings.
//
// Run by ../../test.sh (inside the Steam Runtime image, cwd = build/tests).

#include "roundend_core.hpp"

#include <cstdio>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const std::string& what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

void CheckEq(const std::string& actual, const std::string& expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s\n  expected: [%s]\n  actual:   [%s]\n", what.c_str(), expected.c_str(),
					actual.c_str());
	}
}

void TestLabels() {
	roundend::PlayerSettings settings;
	CheckEq(roundend::SpeedMenuLabel(settings), "Отключить скорость [ ]", "speed label, not disabled");
	CheckEq(roundend::GravityMenuLabel(settings), "Отключить гравитацию [ ]", "gravity label, not disabled");

	settings.disableSpeed = true;
	CheckEq(roundend::SpeedMenuLabel(settings), "Отключить скорость [X]", "speed label, disabled");
	settings.disableGravity = true;
	CheckEq(roundend::GravityMenuLabel(settings), "Отключить гравитацию [X]", "gravity label, disabled");
}

void TestCookieValues() {
	bool out = false;

	Check(roundend::CookieValueToBool(plg::any(static_cast<int64_t>(0)), out) && !out, "int64 0 -> false");
	Check(roundend::CookieValueToBool(plg::any(static_cast<int64_t>(1)), out) && out, "int64 1 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<int32_t>(2)), out) && out, "int32 2 -> true");
	Check(roundend::CookieValueToBool(plg::any(static_cast<uint64_t>(0)), out) && !out, "uint64 0 -> false");
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
	Check(!roundend::CookieValueToBool(plg::any(), out), "empty any is rejected");
}

void TestDefaults() {
	const roundend::Settings defaults;
	Check(defaults.bhopEnabled && defaults.speedEnabled && defaults.gravityEnabled,
		  "the shipped defaults enable all three features");
	Check(defaults.speed == 1.5f && defaults.gravity == 1.5f, "the shipped speed/gravity defaults are 1.5");
	Check(roundend::kDefaultSpeed == 1.0f && roundend::kDefaultGravity == 1.0f,
		  "DEFAULT_SPEED/DEFAULT_GRAVITY are 1.0");
}

} // namespace

int main() {
	TestLabels();
	TestCookieValues();
	TestDefaults();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
