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

MenuChoice ChoiceFromInfo(std::string_view info) {
	if (info == kSpeedInfo) {
		return MenuChoice::Speed;
	}
	if (info == kGravityInfo) {
		return MenuChoice::Gravity;
	}
	return MenuChoice::None;
}

const char* ApplyChoice(PlayerSettings& settings, MenuChoice choice) {
	switch (choice) {
	case MenuChoice::Speed:
		settings.disableSpeed = !settings.disableSpeed;
		return kSpeedChanged;
	case MenuChoice::Gravity:
		settings.disableGravity = !settings.disableGravity;
		return kGravityChanged;
	case MenuChoice::None:
		break;
	}
	return nullptr;
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

Settings ParseSettings(const KvNode& root) {
	Settings settings;
	// The original's LoadConfig(): KeyValues::GetBool/GetFloat with the built-in defaults - a
	// missing or empty key keeps the default, a present numeric value wins.
	settings.bhopEnabled = root.GetBool("bhop_enabled", settings.bhopEnabled);
	settings.speedEnabled = root.GetBool("speed_enabled", settings.speedEnabled);
	settings.gravityEnabled = root.GetBool("gravity_enabled", settings.gravityEnabled);
	settings.speed = root.GetFloat("speed", settings.speed);
	settings.gravity = root.GetFloat("gravity", settings.gravity);
	return settings;
}

std::string JoinPath(std::string_view a, std::string_view b) {
	if (a.empty()) {
		return std::string(b);
	}
	if (b.empty()) {
		return std::string(a);
	}
	std::string out(a);
	if (out.back() != '/') {
		out.push_back('/');
	}
	if (b.front() == '/') {
		b.remove_prefix(1);
	}
	out.append(b);
	return out;
}

std::vector<std::string> ConfigCandidates(std::string_view configsRoot, std::string_view dataRoot) {
	std::vector<std::string> candidates;
	// The raw cwd-relative Metamod path stays first: a server that still keeps its file next to the
	// game binary keeps loading exactly that file (and its values), not a different one from
	// <configs>/RoundEndThings.
	candidates.emplace_back(kConfigRawPath);

	// pisexcompat's ConfigDir() is JoinPath(GetConfigsDir(), "RoundEndThings"); with no host
	// directory that is the relative "RoundEndThings", the same spelling the old port used.
	const std::string pluginDir = configsRoot.empty() ? std::string(kPluginDir) : JoinPath(configsRoot, kPluginDir);
	candidates.push_back(JoinPath(pluginDir, kConfigFileName));
	candidates.push_back(JoinPath(JoinPath(pluginDir, "translations"), kConfigFileName));

	const std::string dataPluginDir = dataRoot.empty() ? std::string(kPluginDir) : JoinPath(dataRoot, kPluginDir);
	candidates.push_back(JoinPath(dataPluginDir, kConfigFileName));
	return candidates;
}

ConfigLoadResult LoadSettings(const std::vector<std::string>& candidates) {
	ConfigLoadResult result;
	if (!candidates.empty()) {
		// pisexcompat's ResolvePath() fallback named the plugin config file when nothing existed.
		result.path = candidates.size() > 1 ? candidates[1] : candidates.front();
	}
	for (const std::string& candidate : candidates) {
		const std::optional<std::string> text = ReadTextFile(candidate);
		if (!text) {
			continue;
		}
		// The first existing file is authoritative: a malformed primary file is reported by the
		// caller and keeps the defaults, it never falls through to a lower-priority copy.
		result.found = true;
		result.path = candidate;
		const std::optional<KvNode> root = ParseKv1(*text);
		if (!root) {
			return result;
		}
		result.parsed = true;
		result.settings = ParseSettings(*root);
		return result;
	}
	return result;
}

} // namespace roundend
