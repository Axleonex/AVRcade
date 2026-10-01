// M5 UNIFY-03: injection-free rules-level cross-check CLI over the real
// vr_safety rule API (loadSafetyRuleSet + SafetyRuleSet::findRule). It reports
// ONLY the rule-level facts the .NET SafetyGate and native vr_safety share
// (matched rule + permitted launch mode). It does NOT run the full injector
// evaluate (which needs identity/preflight the client lacks), and it touches,
// patches, or injects nothing — it reads two files and prints one line.
//
// Contract (M5 plan, M5D5):
//   vrclient_safety_cli --game <path> --rules <path> --mode <launch_mode>
//   -> RULECHECK game_id=<id> rule_found=<true|false> permitted=<true|false> modes=<comma-list>
//   exit 0 always.

#include "config/config_versioning.h"
#include "safety/safety_verdict.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

std::string Arg(const std::vector<std::string>& args, const std::string& key) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == key) return args[i + 1];
  }
  return std::string();
}

std::string ReadGameId(const std::string& gameConfigPath) {
  vrclient::config::JsonValue doc;
  std::string error;
  if (!vrclient::config::parseJson(ReadFile(gameConfigPath), doc, error)) {
    return std::string();
  }
  const vrclient::config::JsonValue* id = doc.find("game_id");
  if (id != nullptr && id->isString()) return id->asString();
  return std::string();
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv, argv + argc);
  const std::string gamePath = Arg(args, "--game");
  const std::string rulesPath = Arg(args, "--rules");
  const std::string mode = Arg(args, "--mode");

  const std::string gameId = ReadGameId(gamePath);

  bool ruleFound = false;
  bool permitted = false;
  std::string modes;

  const vrclient::safety::SafetyRuleLoadResult load =
      vrclient::safety::loadSafetyRuleSet(std::filesystem::path(rulesPath));
  if (load.loaded) {
    const vrclient::safety::SafetyRuleEntry* rule = load.rule_set.findRule(gameId);
    if (rule != nullptr) {
      ruleFound = true;
      for (std::size_t i = 0; i < rule->allowed_launch_modes.size(); ++i) {
        if (i != 0) modes += ",";
        modes += rule->allowed_launch_modes[i];
        if (rule->allowed_launch_modes[i] == mode) permitted = true;
      }
    }
  }

  std::cout << "RULECHECK game_id=" << gameId
            << " rule_found=" << (ruleFound ? "true" : "false")
            << " permitted=" << (permitted ? "true" : "false")
            << " modes=" << modes << "\n";
  return 0;
}
