//
// miniapp: package download cache and installer.
//

#pragma once

#include <string>

namespace miniapp {
namespace packages {

/// Directory that holds all installed mini programs.
std::string root();

/// Installed package directory for one mini program id.
std::string directory(const std::string& id);

/// True when the package exists and matches the requested version.
bool installed(const std::string& id, const std::string& version);

/// Unpacks a ZIP_STORED .nxpkg into the cache and replaces the old version.
bool install(const std::string& id, const std::string& version, const std::string& archive,
             std::string& error);

}  // namespace packages
}  // namespace miniapp
