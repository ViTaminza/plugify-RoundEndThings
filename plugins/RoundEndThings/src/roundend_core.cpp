#include "roundend_core.hpp"

#include <charconv>

namespace roundend {
namespace {

// The original stored "0"/"1" strings via atoi(); accept the same shapes on the way back so a
// cookie written by the Metamod-era plugin (or by any other consumer) keeps its meaning.
bool ParseNumericText(const std::string& text, bool& out) {
	if (text.empty()) {
		return false;
	}
	long long parsed = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if (error != std::errc() || end != text.data() + text.size()) {
		return false;
	}
	out = parsed != 0;
	return true;
}

} // namespace

std::string SpeedMenuLabel(const PlayerSettings& settings) {
	return settings.disableSpeed ? "Отключить скорость [X]" : "Отключить скорость [ ]";
}

std::string GravityMenuLabel(const PlayerSettings& settings) {
	return settings.disableGravity ? "Отключить гравитацию [X]" : "Отключить гравитацию [ ]";
}

bool CookieValueToBool(const plg::any& value, bool& out) {
	if (plg::holds_alternative<bool>(value)) {
		out = plg::get<bool>(value);
		return true;
	}
	if (plg::holds_alternative<int8_t>(value)) {
		out = plg::get<int8_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<int16_t>(value)) {
		out = plg::get<int16_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<int32_t>(value)) {
		out = plg::get<int32_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<int64_t>(value)) {
		out = plg::get<int64_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<uint8_t>(value)) {
		out = plg::get<uint8_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<uint16_t>(value)) {
		out = plg::get<uint16_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<uint32_t>(value)) {
		out = plg::get<uint32_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<uint64_t>(value)) {
		out = plg::get<uint64_t>(value) != 0;
		return true;
	}
	if (plg::holds_alternative<plg::string>(value)) {
		return ParseNumericText(std::string(plg::get<plg::string>(value)), out);
	}
	return false;
}

} // namespace roundend
