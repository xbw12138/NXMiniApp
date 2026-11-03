#pragma once

#include <string>

namespace nxkit {

/// Per-user writable directory used by the framework.
/// - Switch: "sdmc:/switch/nxkit/"
/// - Desktop: "<cwd>/nxkit_data/"
std::string dataDirectory();

/// Returns dataDirectory() joined with `relative`.
std::string dataPath(const std::string& relative);

/// Creates `path` and all of its parent directories (errors are ignored).
void ensureDirectory(const std::string& path);

/// Creates the framework data directory.
void ensureDataDirectory();

}  // namespace nxkit
