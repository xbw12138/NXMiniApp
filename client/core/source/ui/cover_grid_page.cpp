//
// GF: generic paged cover grid page.
//

#include "nxkit/ui/cover_grid_page.hpp"

#include "nxkit/ui/cover_card.hpp"

namespace nxkit {

class CoverGridDataSource : public RecyclingGridDataSource {
public:
    explicit CoverGridDataSource(CoverGridPage* page) : page(page) {}

    size_t getItemCount() override { return page->itemCount(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler,
                                  size_t index) override {
        CoverCard* cell = (CoverCard*)recycler->dequeueReusableCell("CoverCard");
        const CoverItem& item = page->itemAt(index);
        cell->setCard(item.cover, item.title, item.subtitle, item.badge);
        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        page->notifyItemSelected(index);
    }

    void clearData() override { page->clearItems(); }

private:
    CoverGridPage* page;
};

CoverGridPage::CoverGridPage() {
    this->inflateFromXMLRes("xml/views/cover_grid_page.xml");
    this->coverGrid->registerCell("CoverCard", []() { return CoverCard::create(); });
}

void CoverGridPage::setItems(const std::vector<CoverItem>& list) {
    this->items = list;
    this->coverGrid->setDataSource(new CoverGridDataSource(this));
}

void CoverGridPage::appendItems(const std::vector<CoverItem>& list) {
    this->items.insert(this->items.end(), list.begin(), list.end());
    this->coverGrid->notifyDataChanged();
}

void CoverGridPage::clear() { this->setItems({}); }

void CoverGridPage::showSkeleton(unsigned int count) {
    this->coverGrid->showSkeleton(count);
}

void CoverGridPage::setError(const std::string& message) {
    this->coverGrid->setError(message);
}

void CoverGridPage::setEmpty(const std::string& message) {
    this->coverGrid->setEmpty(message);
}

void CoverGridPage::refresh() { this->coverGrid->refresh(); }

void CoverGridPage::forceRequestNextPage() {
    this->coverGrid->forceRequestNextPage();
}

void CoverGridPage::setSpanCount(int count) {
    this->coverGrid->spanCount = count;
    this->coverGrid->reloadData();
}

void CoverGridPage::setItemHeight(float height) {
    this->coverGrid->estimatedRowHeight = height;
    this->coverGrid->reloadData();
}

void CoverGridPage::setOnNextPage(std::function<void()> callback) {
    this->coverGrid->onNextPage(std::move(callback));
}

void CoverGridPage::setRefreshAction(std::function<void()> callback) {
    this->coverGrid->setRefreshAction(std::move(callback));
}

void CoverGridPage::notifyItemSelected(size_t index) {
    if (index >= this->items.size() || !this->onSelect) return;
    this->onSelect(this->items[index]);
}

}  // namespace nxkit
