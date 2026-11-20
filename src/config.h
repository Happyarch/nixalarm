#pragma once

#include <filesystem>

#include "types.h"

namespace fs = std::filesystem;

fs::path default_config_path();
// seed_if_missing = false makes this a read-only load for tools (such as the
// wizard) that must not create a config file as a side effect.
Config load_config(const fs::path& path, bool seed_if_missing = true);
void list_sources(const Config& cfg);
