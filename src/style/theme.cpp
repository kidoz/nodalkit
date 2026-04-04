#include <nk/style/theme.h>

namespace nk {

namespace {

Color rgb(uint8_t r, uint8_t g, uint8_t b) {
    return Color::from_rgb(r, g, b);
}

void set_color_token(Theme& theme, std::string name, uint8_t r, uint8_t g, uint8_t b) {
    theme.set_token(std::move(name), StyleValue{rgb(r, g, b)});
}

void set_metric_token(Theme& theme, std::string name, float value) {
    theme.set_token(std::move(name), StyleValue{value});
}

void add_rule(Theme& theme,
              std::vector<std::string> classes,
              StateFlags pseudo_state,
              std::initializer_list<std::pair<std::string_view, StyleValue>> properties) {

    StyleRule rule;
    rule.selector.classes = std::move(classes);
    rule.selector.pseudo_state = pseudo_state;
    for (const auto& [name, value] : properties) {
        rule.properties.emplace(std::string(name), value);
    }
    theme.add_rule(std::move(rule));
}

void install_shared_rules(Theme& theme) {
    add_rule(theme,
             {"label"},
             StateFlags::None,
             {{"text-color", StyleValue{std::string("text-primary")}}});
    add_rule(theme,
             {"label", "muted"},
             StateFlags::None,
             {{"text-color", StyleValue{std::string("text-secondary")}}});
    add_rule(theme,
             {"label", "heading"},
             StateFlags::None,
             {
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"accent-color", StyleValue{std::string("accent")}},
             });

    add_rule(theme,
             {"card"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-card")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"padding", StyleValue{18.0F}},
                 {"corner-radius", StyleValue{12.0F}},
             });
    add_rule(theme,
             {"page"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("window-bg")}},
                 {"padding", StyleValue{28.0F}},
             });

    add_rule(theme,
             {"menu-bar"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-secondary")}},
                 {"min-height", StyleValue{30.0F}},
             });

    add_rule(theme,
             {"status-bar"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-secondary")}},
                 {"separator-color", StyleValue{std::string("border-subtle")}},
                 {"min-height", StyleValue{std::string("status-height")}},
                 {"segment-gap", StyleValue{18.0F}},
             });

    add_rule(theme,
             {"button"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-raised")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"padding-x", StyleValue{16.0F}},
                 {"padding-y", StyleValue{9.0F}},
                 {"min-width", StyleValue{82.0F}},
                 {"min-height", StyleValue{36.0F}},
                 {"corner-radius", StyleValue{10.0F}},
             });
    add_rule(theme,
             {"button"},
             StateFlags::Hovered,
             {
                 {"background", StyleValue{std::string("surface-hover")}},
                 {"border-color", StyleValue{std::string("border-strong")}},
             });
    add_rule(theme,
             {"button"},
             StateFlags::Pressed,
             {
                 {"background", StyleValue{std::string("surface-pressed")}},
                 {"border-color", StyleValue{std::string("border-strong")}},
             });
    add_rule(theme,
             {"button"},
             StateFlags::Disabled,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-disabled")}},
             });
    add_rule(theme,
             {"button", "suggested"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("accent")}},
                 {"border-color", StyleValue{std::string("accent")}},
                 {"text-color", StyleValue{std::string("accent-contrast")}},
             });
    add_rule(theme,
             {"button", "suggested"},
             StateFlags::Hovered,
             {{"background", StyleValue{std::string("accent-hover")}}});
    add_rule(theme,
             {"button", "suggested"},
             StateFlags::Pressed,
             {{"background", StyleValue{std::string("accent-pressed")}}});
    add_rule(theme,
             {"button", "flat"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-card")}},
                 {"border-color", StyleValue{std::string("surface-card")}},
                 {"text-color", StyleValue{std::string("text-secondary")}},
             });
    add_rule(theme,
             {"button", "flat"},
             StateFlags::Hovered,
             {{"background", StyleValue{std::string("surface-hover")}}});
    add_rule(theme,
             {"button", "flat"},
             StateFlags::Pressed,
             {{"background", StyleValue{std::string("surface-pressed")}}});

    add_rule(theme,
             {"text-field"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("field-bg")}},
                 {"border-color", StyleValue{std::string("field-border")}},
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"placeholder-color", StyleValue{std::string("text-secondary")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"min-height", StyleValue{36.0F}},
                 {"min-width", StyleValue{240.0F}},
                 {"corner-radius", StyleValue{10.0F}},
             });
    add_rule(theme,
             {"text-field"},
             StateFlags::Focused,
             {{"border-color", StyleValue{std::string("focus-ring")}}});
    add_rule(theme,
             {"text-field"},
             StateFlags::Disabled,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"text-color", StyleValue{std::string("text-disabled")}},
             });

    add_rule(theme,
             {"combo-box"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("field-bg")}},
                 {"border-color", StyleValue{std::string("field-border")}},
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"chevron-color", StyleValue{std::string("text-secondary")}},
                 {"popup-background", StyleValue{std::string("surface-card")}},
                 {"popup-border-color", StyleValue{std::string("border-subtle")}},
                 {"popup-hover-background", StyleValue{std::string("surface-hover")}},
                 {"popup-selected-background", StyleValue{std::string("accent-soft")}},
                 {"popup-separator-color", StyleValue{std::string("border-subtle")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"popup-item-height", StyleValue{30.0F}},
                 {"min-height", StyleValue{36.0F}},
                 {"min-width", StyleValue{240.0F}},
                 {"corner-radius", StyleValue{10.0F}},
                 {"popup-radius", StyleValue{12.0F}},
                 {"selection-radius", StyleValue{8.0F}},
             });
    add_rule(theme,
             {"combo-box"},
             StateFlags::Focused,
             {{"border-color", StyleValue{std::string("focus-ring")}}});

    add_rule(theme,
             {"scroll-area"},
             StateFlags::None,
             {
                 {"scrollbar-track-color", StyleValue{std::string("scrollbar-track")}},
                 {"scrollbar-thumb-color", StyleValue{std::string("scrollbar-thumb")}},
             });

    add_rule(theme,
             {"segmented-control"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-secondary")}},
                 {"selected-background", StyleValue{std::string("surface-raised")}},
                 {"selected-border-color", StyleValue{std::string("border-subtle")}},
                 {"selected-text-color", StyleValue{std::string("text-primary")}},
                 {"hover-background", StyleValue{std::string("surface-hover")}},
                 {"pressed-background", StyleValue{std::string("surface-pressed")}},
                 {"separator-color", StyleValue{std::string("border-subtle")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"min-height", StyleValue{36.0F}},
                 {"min-segment-width", StyleValue{84.0F}},
                 {"padding-x", StyleValue{16.0F}},
                 {"track-padding", StyleValue{4.0F}},
                 {"corner-radius", StyleValue{12.0F}},
                 {"selection-radius", StyleValue{10.0F}},
                 {"separator-inset", StyleValue{8.0F}},
             });
    add_rule(theme,
             {"segmented-control"},
             StateFlags::Disabled,
             {
                 {"text-color", StyleValue{std::string("text-disabled")}},
                 {"selected-text-color", StyleValue{std::string("text-disabled")}},
             });

    add_rule(theme,
             {"list-view"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-card")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"selected-background", StyleValue{std::string("accent-soft")}},
                 {"selected-text-color", StyleValue{std::string("text-primary")}},
                 {"row-separator-color", StyleValue{std::string("border-subtle")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"scrollbar-track-color", StyleValue{std::string("scrollbar-track")}},
                 {"scrollbar-thumb-color", StyleValue{std::string("scrollbar-thumb")}},
                 {"corner-radius", StyleValue{12.0F}},
                 {"selection-radius", StyleValue{8.0F}},
             });
    add_rule(theme,
             {"list-view"},
             StateFlags::Focused,
             {{"border-color", StyleValue{std::string("focus-ring")}}});

    add_rule(theme,
             {"image-view"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"corner-radius", StyleValue{14.0F}},
                 {"content-radius", StyleValue{12.0F}},
                 {"min-height", StyleValue{168.0F}},
             });
}

std::shared_ptr<Theme>& active_theme_storage() {
    static std::shared_ptr<Theme> active_theme;
    return active_theme;
}

} // namespace

int StyleSelector::specificity() const {
    int s = 0;
    if (!type_name.empty()) {
        s += 1;
    }
    s += static_cast<int>(classes.size()) * 10;
    if (pseudo_state != StateFlags::None) {
        s += 100;
    }
    return s;
}

struct Theme::Impl {
    std::string name;
    std::vector<StyleRule> rules;
    std::unordered_map<std::string, StyleValue> tokens;
};

Theme::Theme(std::string name) : impl_(std::make_unique<Impl>()) {
    impl_->name = std::move(name);
}

Theme::~Theme() = default;

std::string_view Theme::name() const {
    return impl_->name;
}

void Theme::add_rule(StyleRule rule) {
    impl_->rules.push_back(std::move(rule));
}

void Theme::set_token(std::string name, StyleValue value) {
    impl_->tokens[std::move(name)] = std::move(value);
}

const StyleValue* Theme::token(std::string_view name) const {
    auto it = impl_->tokens.find(std::string(name));
    if (it != impl_->tokens.end()) {
        return &it->second;
    }
    return nullptr;
}

const StyleValue* Theme::resolve(std::string_view type_name,
                                 const std::vector<std::string>& classes,
                                 StateFlags state,
                                 std::string_view property_name) const {

    const StyleRule* best_match = nullptr;
    int best_specificity = -1;

    for (const auto& rule : impl_->rules) {
        const auto& sel = rule.selector;

        // Type must match (or be empty = wildcard).
        if (!sel.type_name.empty() && sel.type_name != type_name) {
            continue;
        }

        // All selector classes must be present.
        bool class_match = true;
        for (const auto& sc : sel.classes) {
            bool found = false;
            for (const auto& c : classes) {
                if (c == sc) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                class_match = false;
                break;
            }
        }
        if (!class_match) {
            continue;
        }

        // Pseudo-state must be a subset of widget state.
        if (sel.pseudo_state != StateFlags::None &&
            (state & sel.pseudo_state) != sel.pseudo_state) {
            continue;
        }

        // Check if this rule has the property.
        auto prop_it = rule.properties.find(std::string(property_name));
        if (prop_it == rule.properties.end()) {
            continue;
        }

        const int spec = sel.specificity();
        if (spec > best_specificity) {
            best_specificity = spec;
            best_match = &rule;
        }
    }

    if (best_match != nullptr) {
        auto it = best_match->properties.find(std::string(property_name));
        if (it != best_match->properties.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

void Theme::set_active(std::shared_ptr<Theme> theme) {
    active_theme_storage() = std::move(theme);
}

std::shared_ptr<Theme> Theme::active() {
    auto& active_theme = active_theme_storage();
    if (!active_theme) {
        active_theme = std::shared_ptr<Theme>(Theme::make_light().release());
    }
    return active_theme;
}

std::unique_ptr<Theme> Theme::make_light() {
    auto theme = std::make_unique<Theme>("Linux Light");

    set_color_token(*theme, "window-bg", 248, 249, 252);
    set_color_token(*theme, "surface-panel", 246, 247, 249);
    set_color_token(*theme, "surface-card", 253, 253, 254);
    set_color_token(*theme, "surface-raised", 247, 248, 250);
    set_color_token(*theme, "surface-hover", 240, 243, 247);
    set_color_token(*theme, "surface-pressed", 232, 236, 242);
    set_color_token(*theme, "field-bg", 255, 255, 255);
    set_color_token(*theme, "field-border", 203, 209, 217);
    set_color_token(*theme, "border-subtle", 224, 228, 234);
    set_color_token(*theme, "border-strong", 191, 198, 208);
    set_color_token(*theme, "text-primary", 37, 40, 46);
    set_color_token(*theme, "text-secondary", 96, 103, 114);
    set_color_token(*theme, "text-disabled", 145, 151, 161);
    set_color_token(*theme, "accent", 53, 132, 228);
    set_color_token(*theme, "accent-hover", 41, 120, 216);
    set_color_token(*theme, "accent-pressed", 32, 104, 192);
    set_color_token(*theme, "accent-soft", 222, 236, 252);
    set_color_token(*theme, "accent-contrast", 255, 255, 255);
    set_color_token(*theme, "focus-ring", 76, 144, 228);
    set_color_token(*theme, "scrollbar-track", 226, 231, 238);
    set_color_token(*theme, "scrollbar-thumb", 170, 180, 192);
    theme->set_token("accent-source", StyleValue{std::string("theme")});
    theme->set_token("motion-mode", StyleValue{std::string("normal")});
    theme->set_token("transparency-mode", StyleValue{std::string("allowed")});
    theme->set_token("density", StyleValue{std::string("standard")});

    set_metric_token(*theme, "spacing-xs", 4.0F);
    set_metric_token(*theme, "spacing-sm", 8.0F);
    set_metric_token(*theme, "spacing-md", 12.0F);
    set_metric_token(*theme, "spacing-lg", 16.0F);
    set_metric_token(*theme, "spacing-xl", 24.0F);
    set_metric_token(*theme, "control-height", 36.0F);
    set_metric_token(*theme, "menu-height", 30.0F);
    set_metric_token(*theme, "status-height", 28.0F);

    install_shared_rules(*theme);

    return theme;
}

std::unique_ptr<Theme> Theme::make_dark() {
    auto theme = std::make_unique<Theme>("Linux Dark");

    set_color_token(*theme, "window-bg", 33, 36, 42);
    set_color_token(*theme, "surface-panel", 39, 43, 50);
    set_color_token(*theme, "surface-card", 46, 50, 58);
    set_color_token(*theme, "surface-raised", 53, 58, 67);
    set_color_token(*theme, "surface-hover", 60, 66, 76);
    set_color_token(*theme, "surface-pressed", 71, 77, 88);
    set_color_token(*theme, "field-bg", 43, 47, 55);
    set_color_token(*theme, "field-border", 83, 89, 101);
    set_color_token(*theme, "border-subtle", 73, 79, 90);
    set_color_token(*theme, "border-strong", 103, 109, 121);
    set_color_token(*theme, "text-primary", 239, 241, 245);
    set_color_token(*theme, "text-secondary", 178, 184, 194);
    set_color_token(*theme, "text-disabled", 128, 134, 145);
    set_color_token(*theme, "accent", 106, 166, 255);
    set_color_token(*theme, "accent-hover", 124, 179, 255);
    set_color_token(*theme, "accent-pressed", 90, 148, 234);
    set_color_token(*theme, "accent-soft", 62, 84, 122);
    set_color_token(*theme, "accent-contrast", 248, 250, 255);
    set_color_token(*theme, "focus-ring", 131, 187, 255);
    set_color_token(*theme, "scrollbar-track", 61, 67, 76);
    set_color_token(*theme, "scrollbar-thumb", 111, 121, 136);
    theme->set_token("accent-source", StyleValue{std::string("theme")});
    theme->set_token("motion-mode", StyleValue{std::string("normal")});
    theme->set_token("transparency-mode", StyleValue{std::string("allowed")});
    theme->set_token("density", StyleValue{std::string("standard")});

    set_metric_token(*theme, "spacing-xs", 4.0F);
    set_metric_token(*theme, "spacing-sm", 8.0F);
    set_metric_token(*theme, "spacing-md", 12.0F);
    set_metric_token(*theme, "spacing-lg", 16.0F);
    set_metric_token(*theme, "spacing-xl", 24.0F);
    set_metric_token(*theme, "control-height", 34.0F);
    set_metric_token(*theme, "menu-height", 32.0F);
    set_metric_token(*theme, "status-height", 28.0F);

    install_shared_rules(*theme);

    return theme;
}

std::unique_ptr<Theme> Theme::make_linux_gnome(ColorScheme color_scheme) {
    auto theme = color_scheme == ColorScheme::Dark ? Theme::make_dark() : Theme::make_light();
    theme->set_token("theme-family", StyleValue{std::string("linux-gnome")});
    return theme;
}

std::unique_ptr<Theme> Theme::make_windows_11(ColorScheme color_scheme) {
    auto theme = Theme::make_linux_gnome(color_scheme);
    theme->set_token("theme-family", StyleValue{std::string("windows-11")});
    return theme;
}

std::unique_ptr<Theme> Theme::make_macos_26(ColorScheme color_scheme) {
    auto theme = Theme::make_linux_gnome(color_scheme);
    theme->set_token("theme-family", StyleValue{std::string("macos-26")});
    return theme;
}

} // namespace nk
