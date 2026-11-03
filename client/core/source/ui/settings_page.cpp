//
// GF: generic settings list built on borealis' RecyclerFrame.
//

#include "nxkit/ui/settings_page.hpp"

#include <algorithm>

#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>

namespace nxkit {

class SettingsDataSource : public brls::RecyclerDataSource {
public:
    explicit SettingsDataSource(SettingsPage* page) : page(page) {}

    int numberOfRows(brls::RecyclerFrame* recycler, int section) override {
        return (int)page->rows.size();
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override {
        return this->makeCell(recycler, index.row);
    }

    void didSelectRowAt(brls::RecyclerFrame* recycler,
                        brls::IndexPath index) override {
        page->activateRow(index.row);
    }

private:
    brls::RecyclerCell* makeCell(brls::RecyclerFrame* recycler,
                                 size_t rowIndex) {
        if (rowIndex >= page->rows.size()) return nullptr;

        // The lambdas capture rowIndex, which is stable until reloadData().
        SettingsPage::Row& row = page->rows[rowIndex];

        switch (row.kind) {
            case SettingsPage::Kind::Header: {
                auto* cell = (brls::RecyclerHeader*)recycler->dequeueReusableCell(
                    "SettingsHeader");
                cell->setTitle(row.title);
                return cell;
            }
            case SettingsPage::Kind::Info:
            case SettingsPage::Kind::Action: {
                auto* cell = (brls::DetailCell*)recycler->dequeueReusableCell(
                    "SettingsDetail");
                cell->setText(row.title);
                cell->setDetailText(row.detail);
                cell->setDetailTextColor(
                    brls::Application::getTheme()["brls/text_disabled"]);
                return cell;
            }
            case SettingsPage::Kind::Input: {
                auto* cell = (brls::InputCell*)recycler->dequeueReusableCell(
                    "SettingsInput");
                cell->getEvent()->clear();
                cell->init(
                    row.title, row.value,
                    [this, rowIndex](std::string value) {
                        page->rows[rowIndex].value = value;
                        if (page->rows[rowIndex].onInput)
                            page->rows[rowIndex].onInput(value);
                    },
                    row.placeholder, row.hint);
                return cell;
            }
            case SettingsPage::Kind::Selector: {
                auto* cell =
                    (brls::SelectorCell*)recycler->dequeueReusableCell(
                        "SettingsSelector");
                cell->getEvent()->clear();
                cell->init(row.title, row.options, row.selected,
                           [this, rowIndex](int selected) {
                               page->rows[rowIndex].selected = selected;
                               if (page->rows[rowIndex].onSelection)
                                   page->rows[rowIndex].onSelection(selected);
                           });
                return cell;
            }
            case SettingsPage::Kind::Toggle: {
                auto* cell = (brls::BooleanCell*)recycler->dequeueReusableCell(
                    "SettingsToggle");
                cell->getEvent()->clear();
                cell->init(row.title, row.toggleValue,
                           [this, rowIndex](bool value) {
                               page->rows[rowIndex].toggleValue = value;
                               if (page->rows[rowIndex].onToggle)
                                   page->rows[rowIndex].onToggle(value);
                           });
                return cell;
            }
        }
        return nullptr;
    }

    SettingsPage* page;
};

SettingsPage::SettingsPage() {
    this->registerCell("SettingsHeader", []() -> brls::RecyclerCell* {
        return brls::RecyclerHeader::create();
    });
    this->registerCell("SettingsDetail", []() -> brls::RecyclerCell* {
        return (brls::RecyclerCell*)brls::DetailCell::create();
    });
    this->registerCell("SettingsInput", []() -> brls::RecyclerCell* {
        return (brls::RecyclerCell*)brls::InputCell::create();
    });
    this->registerCell("SettingsSelector", []() -> brls::RecyclerCell* {
        return (brls::RecyclerCell*)brls::SelectorCell::create();
    });
    this->registerCell("SettingsToggle", []() -> brls::RecyclerCell* {
        return (brls::RecyclerCell*)brls::BooleanCell::create();
    });

    this->setPadding(20, 40, 40, 40);
    this->setDataSource(new SettingsDataSource(this));
}

void SettingsPage::addSection(const std::string& title) {
    Row row;
    row.kind = Kind::Header;
    row.title = title;
    this->rows.push_back(std::move(row));
}

void SettingsPage::addInfo(const std::string& title, const std::string& detail) {
    Row row;
    row.kind = Kind::Info;
    row.title = title;
    row.detail = detail;
    this->rows.push_back(std::move(row));
}

void SettingsPage::addAction(const std::string& title,
                             const std::string& detail,
                             std::function<void()> callback) {
    Row row;
    row.kind = Kind::Action;
    row.title = title;
    row.detail = detail;
    row.onAction = std::move(callback);
    this->rows.push_back(std::move(row));
}

void SettingsPage::addInput(const std::string& title,
                            const std::string& value,
                            std::function<void(const std::string&)> callback,
                            const std::string& placeholder,
                            const std::string& hint) {
    Row row;
    row.kind = Kind::Input;
    row.title = title;
    row.value = value;
    row.placeholder = placeholder;
    row.hint = hint;
    row.onInput = std::move(callback);
    this->rows.push_back(std::move(row));
}

void SettingsPage::addSelector(const std::string& title,
                               const std::vector<std::string>& options,
                               int selected,
                               std::function<void(int)> callback) {
    if (options.empty()) return;

    Row row;
    row.kind = Kind::Selector;
    row.title = title;
    row.options = options;
    row.selected = std::max(0, std::min(selected, (int)options.size() - 1));
    row.onSelection = std::move(callback);
    this->rows.push_back(std::move(row));
}

void SettingsPage::addToggle(const std::string& title, bool value,
                             std::function<void(bool)> callback) {
    Row row;
    row.kind = Kind::Toggle;
    row.title = title;
    row.toggleValue = value;
    row.onToggle = std::move(callback);
    this->rows.push_back(std::move(row));
}

void SettingsPage::reload() { this->reloadData(); }

void SettingsPage::clear() {
    this->rows.clear();
    this->reloadData();
}

void SettingsPage::activateRow(size_t row) {
    if (row >= this->rows.size()) return;
    if (this->rows[row].kind != Kind::Action) return;
    if (this->rows[row].onAction) this->rows[row].onAction();
}

}  // namespace nxkit
