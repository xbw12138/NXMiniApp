//
// GF: generic QR code login view.
//

#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>

namespace nxkit {

class QrCodeView;

/// Shows a QR code, a status line and a refresh button.
///
/// The framework only draws the QR code; the app owns the login flow:
///   auto* view = new QRLoginView();
///   view->setOnRefresh([this] { requestQrCode(); });
///   view->setStatus("Scan with the mobile app");
///   view->setQrContent(qrUrl);
class QRLoginView : public brls::Box {
public:
    QRLoginView();

    /// Renders `content` (usually a URL) as a QR code.
    void setQrContent(const std::string& content);

    /// Sets the hint shown under the QR code.
    void setStatus(const std::string& status);

    /// Replaces the QR code with a "loading" status.
    void showLoading(const std::string& message);

    void setOnRefresh(std::function<void()> callback) {
        onRefresh = std::move(callback);
    }

    void setRefreshVisible(bool visible);

    static brls::View* create() { return new QRLoginView(); }

private:
    QrCodeView* qrImage = nullptr;
    brls::Label* statusLabel = nullptr;
    brls::Button* refreshButton = nullptr;
    std::function<void()> onRefresh;
};

}  // namespace nxkit
