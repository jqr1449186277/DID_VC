#include "ttss_artifacts.hpp"

void TTSSArtifactsWriter::save_setup(const std::filesystem::path& dir,
                                     const nlohmann::json& setupJson,
                                     const didzk::TTSSShareSetupResult& shareSetup) {
  save_json_pretty(dir / "ttss_setup.json", setupJson);

  const nlohmann::json ttss = setupJson.value("ttss", nlohmann::json::object());
  const nlohmann::json vkJson = {
      {"scheme", std::string(didzk::kTTSSSchemeNitsShamirV1)},
      {"id", setupJson.value("id", std::string())},
      {"idHash", setupJson.value("idHash", std::string())},
      {"ver", setupJson.value("ver", 0ull)},
      {"epoch", setupJson.value("epoch", 0ull)},
      {"n", setupJson.value("n", 0)},
      {"t", setupJson.value("t", 0)},
      {"vk", key_entries_to_json(shareSetup.vk)},
      {"vkSetHash", ttss.value("vkSetHash", shareSetup.vkSetHash)},
  };
  const nlohmann::json tkJson = {
      {"scheme", std::string(didzk::kTTSSSchemeNitsShamirV1)},
      {"id", setupJson.value("id", std::string())},
      {"idHash", setupJson.value("idHash", std::string())},
      {"ver", setupJson.value("ver", 0ull)},
      {"epoch", setupJson.value("epoch", 0ull)},
      {"n", setupJson.value("n", 0)},
      {"t", setupJson.value("t", 0)},
      {"tk", key_entries_to_json(shareSetup.tk)},
  };
  const nlohmann::json ttssMetaJson = {
      {"scheme", std::string(didzk::kTTSSSchemeNitsShamirV1)},
      {"id", setupJson.value("id", std::string())},
      {"idHash", setupJson.value("idHash", std::string())},
      {"ver", setupJson.value("ver", 0ull)},
      {"epoch", setupJson.value("epoch", 0ull)},
      {"n", setupJson.value("n", 0)},
      {"t", setupJson.value("t", 0)},
      {"secretType", "srec_seed"},
      {"guardianSetHash", ttss.value("guardianSetHash", std::string())},
      {"vkSetHash", ttss.value("vkSetHash", shareSetup.vkSetHash)},
      {"dealerPkHex", ttss.value("dealerPkHex", std::string())},
      {"rhoCommitHex", ttss.value("rhoCommitHex", shareSetup.rhoCommitHex)},
      {"metaHash", ttss.value("metaHash", std::string())},
  };

  save_json_pretty(dir / "ttss_vk.json", vkJson);
  save_json_pretty(dir / "ttss_tk.json", tkJson);
  save_json_pretty(dir / "ttss_meta.json", ttssMetaJson);

  nlohmann::json shares = nlohmann::json::array();
  for (const auto& env : shareSetup.shareEnvelopes) shares.push_back(didzk::share_envelope_to_njson(env));
  save_json_pretty(dir / "ttss_share_manifest.json", shares);
}

void save_ttss_setup_artifacts(const std::filesystem::path& dir,
                               const nlohmann::json& setupJson,
                               const didzk::TTSSShareSetupResult& shareSetup) {
  TTSSArtifactsWriter::save_setup(dir, setupJson, shareSetup);
}
