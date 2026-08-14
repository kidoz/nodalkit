#pragma once

/// @file about_dialog.h
/// @brief GNOME-style About window for an application.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/widgets/dialog.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class Window;

/// The information a GNOME About window presents. Only `application_name` is
/// required; every other field is omitted from the dialog when left empty.
struct AboutInfo {
    std::string application_name;
    std::string version;
    /// One-line description of what the application does.
    std::string comments;
    /// The person or organization behind the application.
    std::string developer_name;
    std::string website;
    std::string issue_url;
    std::string copyright;
    /// Short license name, for example "GPL-3.0-or-later".
    std::string license;
    std::vector<std::string> credits;
};

/// An About window following the GNOME pattern: application identity first,
/// then a boxed list of links, then legal information.
///
/// Users expect a specific shape here, so prefer this over assembling an
/// ad-hoc dialog with a version string in the message body.
class AboutDialog {
public:
    [[nodiscard]] static std::shared_ptr<AboutDialog> create(AboutInfo info);
    ~AboutDialog();

    AboutDialog(const AboutDialog&) = delete;
    AboutDialog& operator=(const AboutDialog&) = delete;

    /// Present the About window modally over `parent`.
    void present(Window& parent);

    /// Emitted with the target URL when the user activates a link row. The
    /// toolkit does not open URLs itself; the application decides how.
    Signal<std::string_view>& on_link_activated();

    /// The underlying dialog, for callers that need to adjust presentation.
    [[nodiscard]] Dialog& dialog();

private:
    explicit AboutDialog(AboutInfo info);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
