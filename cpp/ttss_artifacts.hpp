#pragma once

#include "ttss_flow.hpp"

class TTSSArtifactsWriter {
 public:
  static void save_setup(const std::filesystem::path& dir,
                         const nlohmann::json& setupJson,
                         const didzk::TTSSShareSetupResult& shareSetup);
};
