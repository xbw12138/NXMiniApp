//
// GF: generic QR code login view.
//

#include "nxkit/ui/qr_login_view.hpp"

#include <borealis/views/button.hpp>
#include <qrcodegen.hpp>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace nxkit {

/// Draws the QR modules straight into the NanoVG context. Avoids going through
/// SVG -> bitmap -> texture, which is fragile on the Switch renderer.
class QrCodeView : public brls::View {
public:
    QrCodeView() { this->setClipsToBounds(false); }

    bool setQrContent(const std::string& content) {
        this->modules.clear();
        this->size = 0;

        if (content.empty()) {
            this->invalidate();
            return false;
        }

        try {
            qrcodegen::QrCode qr =
                qrcodegen::QrCode::encodeText(content.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            this->size = qr.getSize();
            this->modules.resize(static_cast<size_t>(this->size) * this->size);
            for (int y = 0; y < this->size; y++) {
                for (int x = 0; x < this->size; x++) {
                    this->modules[static_cast<size_t>(y) * this->size + x] =
                        qr.getModule(x, y) ? 1 : 0;
                }
            }
        } catch (const std::exception&) {
            this->size = 0;
            this->modules.clear();
            this->invalidate();
            return false;
        }

        this->invalidate();
        return true;
    }

    void clearQr() {
        if (this->modules.empty()) return;
        this->size = 0;
        this->modules.clear();
        this->invalidate();
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override {
        if (this->size <= 0 || this->modules.empty()) return;

        const int border = 4;
        const int total  = this->size + border * 2;

        float cell = std::min(width, height) / static_cast<float>(total);
        if (cell <= 0) return;
        float qr   = cell * total;
        float ox   = x + (width - qr) / 2.0f;
        float oy   = y + (height - qr) / 2.0f;

        // White quiet zone; scanners need the light border around the modules.
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ox, oy, qr, qr, 12.0f);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);

        nvgBeginPath(vg);
        for (int my = 0; my < this->size; my++) {
            for (int mx = 0; mx < this->size; mx++) {
                if (!this->modules[static_cast<size_t>(my) * this->size + mx]) continue;
                float px = ox + static_cast<float>(mx + border) * cell;
                float py = oy + static_cast<float>(my + border) * cell;
                // Slight overlap so fractional cell sizes leave no seams.
                nvgRect(vg, px, py, cell + 0.5f, cell + 0.5f);
            }
        }
        nvgFillColor(vg, nvgRGB(16, 16, 16));
        nvgFill(vg);
    }

private:
    int size = 0;
    std::vector<unsigned char> modules;
};

QRLoginView::QRLoginView() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::CENTER);

    this->qrImage = new QrCodeView();
    this->qrImage->setDimensions(320, 320);
    this->qrImage->setVisibility(brls::Visibility::GONE);
    this->addView(this->qrImage);

    this->statusLabel = new brls::Label();
    this->statusLabel->setFontSize(18);
    this->statusLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->statusLabel->setMarginTop(24);
    this->addView(this->statusLabel);

    this->refreshButton = new brls::Button();
    this->refreshButton->setText("Refresh");
    this->refreshButton->setMarginTop(16);
    this->refreshButton->setVisibility(brls::Visibility::GONE);
    this->refreshButton->registerClickAction([this](brls::View* view) {
        if (this->onRefresh) this->onRefresh();
        return true;
    });
    this->addView(this->refreshButton);
}

void QRLoginView::setQrContent(const std::string& content) {
    if (content.empty() || !this->qrImage->setQrContent(content)) {
        this->qrImage->setVisibility(brls::Visibility::GONE);
        return;
    }
    this->qrImage->setVisibility(brls::Visibility::VISIBLE);
}

void QRLoginView::setStatus(const std::string& status) {
    this->statusLabel->setText(status);
}

void QRLoginView::showLoading(const std::string& message) {
    this->qrImage->clearQr();
    this->qrImage->setVisibility(brls::Visibility::GONE);
    this->setStatus(message);
}

void QRLoginView::setRefreshVisible(bool visible) {
    this->refreshButton->setVisibility(
        visible ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

}  // namespace nxkit
