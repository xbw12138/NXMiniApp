//
// GF: generic cover card used by every nxkit app.
//

#pragma once

#include <borealis.hpp>

#include "nxkit/ui/recycling_grid.hpp"

class TextBox;

/// A recycler item showing a cover image with a title, a subtitle and an
/// optional badge drawn over the cover.
class CoverCard : public RecyclingGridItem {
public:
    CoverCard();

    ~CoverCard() override;

    /// `cover` is a remote image url (empty shows the placeholder).
    /// `badge` is drawn at the bottom left of the cover, hidden when empty.
    void setCard(const std::string& cover, const std::string& title,
                 const std::string& subtitle = "",
                 const std::string& badge = "");

    void prepareForReuse() override;

    void cacheForReuse() override;

    static CoverCard* create();

private:
    BRLS_BIND(brls::Image, picture, "cover/card/picture");
    BRLS_BIND(TextBox, labelTitle, "cover/card/label/title");
    BRLS_BIND(brls::Label, labelSubtitle, "cover/card/label/subtitle");
    BRLS_BIND(brls::Label, labelBadge, "cover/card/label/badge");
};
