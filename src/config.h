#pragma once

#include <filesystem>

#include "types.h"

namespace fs = std::filesystem;

fs::path default_config_path();
Config load_config(const fs::path& path);
void list_sources(const Config& cfg);
