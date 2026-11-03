//
// GF: generic paged cover grid page.
//

#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

#include "nxkit/ui/recycling_grid.hpp"

namespace nxkit {

/// One entry of a CoverGridPage.
struct CoverItem {
    std::string id;
    std::string cover;
    std::string title;
    std::string subtitle;
    std::string badge;
};

/// A scrolling grid of CoverCard, with built in skeleton, empty, error and
/// infinite scrolling states. Subclass it (or compose it) for every app that
/// needs a "browse a list of media" screen.
///
/// The class is reusable as-is, e.g.:
///   auto* page = new CoverGridPage();
///   page->setOnSelect([this](const CoverItem& item) { ... });
///   page->setOnNextPage([this] { loadNextPage(); });
///   page->showSkeleton();
///   page->setItems(items);
class CoverGridPage : public brls::Box {
public:
    CoverGridPage();

    /// Replaces the whole content.
    void setItems(const std::vector<CoverItem>& items);

    /// Appends a page of content (used by infinite scrolling).
    void appendItems(const std::vector<CoverItem>& items);

    void clear();

    void showSkeleton(unsigned int count = 12);
    void setError(const std::string& message);
    void setEmpty(const std::string& message = "");

    /// Triggers the refresh button animation and calls the refresh action.
    void refresh();

    /// Allows the next page callback to fire again.
    void forceRequestNextPage();

    /// Number of columns and card height, forwarded to the grid.
    void setSpanCount(int count);
    void setItemHeight(float height);

    void setOnSelect(std::function<void(const CoverItem&)> callback) {
        onSelect = std::move(callback);
    }
    void setOnNextPage(std::function<void()> callback);
    void setRefreshAction(std::function<void()> callback);

    RecyclingGrid* grid() { return coverGrid; }

    // Used by the internal data source.
    size_t itemCount() const { return items.size(); }
    const CoverItem& itemAt(size_t index) const { return items.at(index); }
    void notifyItemSelected(size_t index);
    void clearItems() { items.clear(); }

    static brls::View* create() { return new CoverGridPage(); }

private:
    BRLS_BIND(RecyclingGrid, coverGrid, "cover/page/grid");

    std::vector<CoverItem> items;
    std::function<void(const CoverItem&)> onSelect;
};

}  // namespace nxkit
