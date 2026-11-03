//
// GF: generic settings list built on borealis' RecyclerFrame.
//

#pragma once

#include <borealis.hpp>
#include <borealis/views/recycler.hpp>

#include <functional>
#include <string>
#include <vector>

namespace nxkit {

class SettingsDataSource;

/// A reusable settings screen.
///
/// Rows are added in order, sections group them visually:
///   auto* settings = new SettingsPage();
///   settings->addSection("Network");
///   settings->addInput("API server", url, [](const std::string& v) { ... });
///   settings->addToggle("Use cache", true, [](bool v) { ... });
///   settings->reload();
class SettingsPage : public brls::RecyclerFrame {
public:
    SettingsPage();

    void addSection(const std::string& title);
    void addInfo(const std::string& title, const std::string& detail);
    void addAction(const std::string& title, const std::string& detail,
                   std::function<void()> callback);
    void addInput(const std::string& title, const std::string& value,
                  std::function<void(const std::string&)> callback,
                  const std::string& placeholder = "",
                  const std::string& hint = "");
    void addSelector(const std::string& title,
                     const std::vector<std::string>& options, int selected,
                     std::function<void(int)> callback);
    void addToggle(const std::string& title, bool value,
                   std::function<void(bool)> callback);

    /// Rebuilds the list, call it after adding rows.
    void reload();

    /// Removes every row (the list is not refreshed until reload()).
    void clear();

    static brls::View* create() { return new SettingsPage(); }

private:
    friend class SettingsDataSource;

    enum class Kind { Header, Info, Action, Input, Selector, Toggle };

    struct Row {
        Kind kind = Kind::Info;
        std::string title;
        std::string detail;
        std::string value;
        std::string placeholder;
        std::string hint;
        std::vector<std::string> options;
        int selected = 0;
        bool toggleValue = false;

        std::function<void()> onAction;
        std::function<void(const std::string&)> onInput;
        std::function<void(int)> onSelection;
        std::function<void(bool)> onToggle;
    };

    void activateRow(size_t row);

    std::vector<Row> rows;
};

}  // namespace nxkit
