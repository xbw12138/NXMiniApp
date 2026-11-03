//
// GF: generic cover card used by every nxkit app.
//

#include "nxkit/ui/cover_card.hpp"

#include "nxkit/ui/text_box.hpp"
#include "nxkit/utils/image_helper.hpp"

CoverCard::CoverCard() { this->inflateFromXMLRes("xml/views/cover_card.xml"); }

CoverCard::~CoverCard() { ImageHelper::clear(this->picture); }

void CoverCard::setCard(const std::string& cover, const std::string& title,
                        const std::string& subtitle, const std::string& badge) {
    this->labelTitle->setText(title);

    this->labelSubtitle->setText(subtitle);
    this->labelSubtitle->setVisibility(subtitle.empty()
                                           ? brls::Visibility::GONE
                                           : brls::Visibility::VISIBLE);

    this->labelBadge->setText(badge);
    this->labelBadge->setVisibility(badge.empty() ? brls::Visibility::GONE
                                                  : brls::Visibility::VISIBLE);

    if (cover.empty())
        this->picture->setImageFromRes("pictures/cover-bg.png");
    else
        ImageHelper::with(this->picture)->load(cover);
}

void CoverCard::prepareForReuse() {
    this->picture->setImageFromRes("pictures/cover-bg.png");
    this->labelTitle->setText("");
    this->labelSubtitle->setText("");
    this->labelBadge->setText("");
    this->labelSubtitle->setVisibility(brls::Visibility::GONE);
    this->labelBadge->setVisibility(brls::Visibility::GONE);
}

void CoverCard::cacheForReuse() { ImageHelper::clear(this->picture); }

CoverCard* CoverCard::create() { return new CoverCard(); }
