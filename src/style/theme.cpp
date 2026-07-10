#include <nk/style/theme.h>
#include <nk/style/theme_defaults.h>

namespace nk {

namespace {

Color rgb(uint8_t r, uint8_t g, uint8_t b) {
    return Color::from_rgb(r, g, b);
}

void set_color_token(Theme& theme, std::string name, uint8_t r, uint8_t g, uint8_t b) {
    theme.set_token(std::move(name), StyleValue{rgb(r, g, b)});
}

void set_color_token(Theme& theme, std::string name, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    auto color = rgb(r, g, b);
    color.a = alpha;
    theme.set_token(std::move(name), StyleValue{color});
}

void set_metric_token(Theme& theme, std::string name, float value) {
    theme.set_token(std::move(name), StyleValue{value});
}

// A rule property value that aliases a token by name. The alias is dereferenced
// at widget-read time (resolve_widget_rule / the global token fallback), so
// post-build token mutations — density, text scale, high contrast, accent
// override — propagate into resolved rule values instead of going stale.
StyleValue token_ref(std::string_view token_name) {
    return StyleValue{std::string(token_name)};
}

// Register a deprecated token name that resolves to its canonical replacement.
void set_alias_token(Theme& theme, std::string name, std::string_view target) {
    theme.set_token(std::move(name), token_ref(target));
}

// Deprecated token names kept as aliases while the C1 surface-naming migration
// settles: surfaces are `surface-*`, borders are `border-*`, and the `*-bg`
// suffix is reserved for the window/shell-layer backgrounds.
void install_legacy_token_aliases(Theme& theme) {
    set_alias_token(theme, "field-bg", "surface-field");
    set_alias_token(theme, "field-border", "border-field");

    // Libadwaita-compatible semantic surface roles. Keeping these as aliases
    // lets platform families retain their own palettes while widgets consume a
    // stable background/foreground vocabulary.
    set_alias_token(theme, "window-fg", "text-primary");
    set_alias_token(theme, "view-bg", "surface-field");
    set_alias_token(theme, "view-fg", "text-primary");
    set_alias_token(theme, "headerbar-bg", "surface-card");
    set_alias_token(theme, "headerbar-fg", "text-primary");
    set_alias_token(theme, "headerbar-backdrop", "window-bg");
    set_alias_token(theme, "headerbar-border", "border-subtle");
    set_alias_token(theme, "headerbar-shade", "border-subtle");
    set_alias_token(theme, "sidebar-bg", "surface-panel");
    set_alias_token(theme, "sidebar-fg", "text-primary");
    set_alias_token(theme, "sidebar-backdrop", "surface-panel");
    set_alias_token(theme, "sidebar-border", "border-subtle");
    set_alias_token(theme, "secondary-sidebar-bg", "surface-raised");
    set_alias_token(theme, "secondary-sidebar-fg", "text-primary");
    set_alias_token(theme, "card-bg", "surface-card");
    set_alias_token(theme, "card-fg", "text-primary");
    set_alias_token(theme, "dialog-bg", "surface-card");
    set_alias_token(theme, "dialog-fg", "text-primary");
    set_alias_token(theme, "popover-bg", "surface-card");
    set_alias_token(theme, "popover-fg", "text-primary");
}

// Paired selection tokens (R2): the active-window selection highlight and the
// text on it, plus the muted pair used while the window is inactive. Families
// choose native values — GNOME/Windows keep the soft accent wash, macOS uses
// the solid accent with contrast text.
void install_selection_tokens(Theme& theme,
                              std::string_view active_bg,
                              std::string_view active_text) {
    set_alias_token(theme, "selection-active-bg", active_bg);
    set_alias_token(theme, "selection-active-text", active_text);
    set_alias_token(theme, "selection-inactive-bg", "surface-pressed");
    set_alias_token(theme, "selection-inactive-text", "text-primary");
}

// Per-family type scale (R10): showcase and application text roles resolve
// these instead of hardcoding point sizes.
void install_type_scale_tokens(Theme& theme, float title, float body, float caption, float value) {
    set_metric_token(theme, "font-size-title", title);
    set_metric_token(theme, "font-size-body", body);
    set_metric_token(theme, "font-size-heading", body + 1.0F);
    set_metric_token(theme, "font-size-document", body + 1.0F);
    set_metric_token(theme, "font-size-caption", caption);
    set_metric_token(theme, "font-size-value", value);
}

// Named metric tokens whose value is currently the same in every family. The
// per-family personality lives in the spacing scale, control heights, radius
// roles, and control padding set by each factory; everything here exists so no
// rule property needs a magic literal.
void install_shared_metric_tokens(Theme& theme) {
    set_metric_token(theme, "padding-card", 18.0F);
    set_metric_token(theme, "padding-page", 28.0F);
    set_metric_token(theme, "segment-gap", 18.0F);
    set_metric_token(theme, "field-min-width", 240.0F);
    set_metric_token(theme, "popup-item-height", 30.0F);
    set_metric_token(theme, "segment-min-width", 84.0F);
    set_metric_token(theme, "image-min-height", 168.0F);
    set_metric_token(theme, "headerbar-height", 46.0F);
    set_metric_token(theme, "headerbar-control-target", 46.0F);
    set_metric_token(theme, "headerbar-control-size", 34.0F);
    set_metric_token(theme, "headerbar-icon-size", 12.0F);
    set_metric_token(theme, "headerbar-padding-x", 6.0F);
    set_metric_token(theme, "clamp-maximum-size", 720.0F);
    set_metric_token(theme, "clamp-tightening-threshold", 540.0F);
    set_metric_token(theme, "adaptive-sidebar-width", 280.0F);
    set_metric_token(theme, "toolbar-view-separator-width", 1.0F);
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

// Base rules: lowest-specificity wildcard mappings so every widget — including
// the ones without a dedicated rule block — resolves the generic text, border,
// focus, and selection vocabulary from semantic tokens instead of leaking
// light-theme fallback literals into dark, high-contrast, or accent-overridden
// themes. The property → token pairs are the central fallback table itself
// (theme_defaults.h), so the safety net and the live cascade cannot drift
// apart. Any class rule (specificity 10+) overrides these (specificity 0).
void install_base_rules(Theme& theme) {
    StyleRule base;
    for (const auto& entry : ThemeColorDefaults) {
        base.properties.emplace(std::string(entry.property), token_ref(entry.token));
    }
    theme.add_rule(std::move(base));

    // Disabled widgets dim their text everywhere; a widget's own :Disabled
    // rule (specificity 110) still overrides this (specificity 100).
    add_rule(theme,
             {},
             StateFlags::Disabled,
             {
                 {"text-color", token_ref("text-disabled")},
                 {"selected-text-color", token_ref("text-disabled")},
             });
}

void install_shared_rules(Theme& theme) {
    install_base_rules(theme);

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
                 {"padding", token_ref("padding-card")},
                 {"corner-radius", token_ref("radius-card")},
             });
    add_rule(theme,
             {"page"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("window-bg")}},
                 {"padding", token_ref("padding-page")},
             });

    add_rule(theme,
             {"menu-bar"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-secondary")}},
                 {"min-height", StyleValue{std::string("menu-height")}},
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
                 {"segment-gap", token_ref("segment-gap")},
             });

    add_rule(theme,
             {"button"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-raised")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"padding-x", token_ref("padding-control-x")},
                 {"padding-y", token_ref("padding-control-y")},
                 {"min-width", token_ref("control-min-width")},
                 {"min-height", token_ref("control-height")},
                 {"corner-radius", token_ref("radius-control")},
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
             {"navigation-row"},
             StateFlags::None,
             {{"selected-background", StyleValue{std::string("accent-soft")}},
              {"hover-background", StyleValue{std::string("surface-hover")}},
              {"pressed-background", StyleValue{std::string("surface-pressed")}},
              {"text-color", StyleValue{std::string("text-primary")}},
              {"focus-ring-color", StyleValue{std::string("focus-ring")}},
              {"font-size", token_ref("font-size-body")}});
    add_rule(theme,
             {"primary-menu-button"},
             StateFlags::None,
             {{"hover-background", StyleValue{std::string("surface-hover")}},
              {"pressed-background", StyleValue{std::string("surface-pressed")}},
              {"text-color", StyleValue{std::string("text-primary")}},
              {"focus-ring-color", StyleValue{std::string("focus-ring")}}});

    add_rule(theme,
             {"text-field"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-field")}},
                 {"border-color", StyleValue{std::string("border-field")}},
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"placeholder-color", StyleValue{std::string("text-secondary")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"min-height", token_ref("control-height")},
                 {"min-width", token_ref("field-min-width")},
                 {"corner-radius", token_ref("radius-control")},
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
                 {"background", StyleValue{std::string("surface-field")}},
                 {"border-color", StyleValue{std::string("border-field")}},
                 {"text-color", StyleValue{std::string("text-primary")}},
                 {"chevron-color", StyleValue{std::string("text-secondary")}},
                 {"chevron-style", StyleValue{std::string("divided")}},
                 {"popup-background", StyleValue{std::string("surface-card")}},
                 {"popup-border-color", StyleValue{std::string("border-subtle")}},
                 {"popup-hover-background", StyleValue{std::string("surface-hover")}},
                 {"popup-selected-background", StyleValue{std::string("accent-soft")}},
                 {"popup-separator-color", StyleValue{std::string("border-subtle")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"popup-item-height", token_ref("popup-item-height")},
                 {"min-height", token_ref("control-height")},
                 {"min-width", token_ref("field-min-width")},
                 {"corner-radius", token_ref("radius-control")},
                 {"popup-radius", token_ref("radius-popup")},
                 {"selection-radius", token_ref("radius-selection")},
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
                 {"min-height", token_ref("control-height")},
                 {"min-segment-width", token_ref("segment-min-width")},
                 {"padding-x", token_ref("padding-control-x")},
                 {"track-padding", token_ref("segment-track-padding")},
                 {"corner-radius", token_ref("radius-segment")},
                 {"selection-radius", token_ref("radius-control")},
                 {"separator-inset", token_ref("spacing-sm")},
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
                 {"selected-background", token_ref("selection-active-bg")},
                 {"selected-text-color", token_ref("selection-active-text")},
                 {"inactive-selected-background", token_ref("selection-inactive-bg")},
                 {"inactive-selected-text-color", token_ref("selection-inactive-text")},
                 {"row-separator-color", StyleValue{std::string("border-subtle")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"scrollbar-track-color", StyleValue{std::string("scrollbar-track")}},
                 {"scrollbar-thumb-color", StyleValue{std::string("scrollbar-thumb")}},
                 {"corner-radius", token_ref("radius-card")},
                 {"selection-radius", token_ref("radius-selection")},
             });
    // The list viewport ring is a keyboard-focus affordance; pointer or
    // programmatic focus must not paint it (R3).
    add_rule(theme,
             {"list-view"},
             StateFlags::FocusVisible,
             {{"border-color", StyleValue{std::string("focus-ring")}}});

    add_rule(theme,
             {"image-view"},
             StateFlags::None,
             {
                 {"background", StyleValue{std::string("surface-panel")}},
                 {"border-color", StyleValue{std::string("border-subtle")}},
                 {"focus-ring-color", StyleValue{std::string("focus-ring")}},
                 {"corner-radius", token_ref("radius-image")},
                 {"content-radius", token_ref("radius-image-content")},
                 {"min-height", token_ref("image-min-height")},
             });

    // --- Coverage rules ---
    // Every remaining widget class maps its color properties to semantic
    // tokens here so no widget renders light-theme fallback literals in dark,
    // high-contrast, or accent-overridden themes. Generic properties
    // (text-color, border-color, focus-ring-color, …) already resolve through
    // the base rules; these blocks carry backgrounds and widget-specific
    // roles. Known gap: the info-bar severity backgrounds stay widget-computed
    // until severity tokens exist.

    add_rule(theme,
             {"avatar"},
             StateFlags::None,
             {
                 // Neutral placeholder swatch behind initials.
                 {"background", token_ref("border-strong")},
                 {"text-color", token_ref("accent-contrast")},
             });

    add_rule(theme,
             {"badge"},
             StateFlags::None,
             {
                 {"background", token_ref("accent")},
                 {"text-color", token_ref("accent-contrast")},
             });

    add_rule(theme,
             {"breadcrumb"},
             StateFlags::None,
             {
                 {"link-color", token_ref("accent")},
                 {"hover-color", token_ref("accent-hover")},
                 {"separator-color", token_ref("text-disabled")},
             });

    add_rule(theme,
             {"calendar"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-field")},
                 {"hover-color", token_ref("surface-hover")},
                 {"selected-text-color", token_ref("accent-contrast")},
             });

    add_rule(theme,
             {"check-box"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-field")},
                 {"border-color", token_ref("border-field")},
                 {"checked-background", token_ref("accent")},
                 {"check-color", token_ref("accent-contrast")},
             });

    add_rule(
        theme, {"color-well"}, StateFlags::None, {{"border-color", token_ref("border-strong")}});

    add_rule(theme,
             {"command-palette"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-card")},
                 {"search-background", token_ref("surface-panel")},
                 {"search-border-color", token_ref("border-subtle")},
             });

    add_rule(theme,
             {"data-table"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-card")},
                 {"header-background", token_ref("surface-panel")},
                 {"cell-border-color", token_ref("border-subtle")},
                 {"selected-background", token_ref("selection-active-bg")},
                 {"selected-text-color", token_ref("selection-active-text")},
                 {"inactive-selected-background", token_ref("selection-inactive-bg")},
                 {"inactive-selected-text-color", token_ref("selection-inactive-text")},
             });

    add_rule(theme,
             {"dialog"},
             StateFlags::None,
             {
                 {"dialog-background", token_ref("surface-card")},
                 {"dialog-border-color", token_ref("border-subtle")},
                 {"dialog-title-color", token_ref("text-primary")},
                 {"dialog-text-color", token_ref("text-secondary")},
                 {"dialog-button-background", token_ref("surface-raised")},
                 {"dialog-button-border", token_ref("border-subtle")},
                 {"dialog-button-text", token_ref("text-primary")},
             });

    add_rule(theme,
             {"expander"},
             StateFlags::None,
             {
                 {"header-background", token_ref("surface-panel")},
                 {"arrow-color", token_ref("text-secondary")},
             });

    add_rule(theme,
             {"grid-view"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-card")},
                 {"cell-border-color", token_ref("border-subtle")},
                 {"selected-background", token_ref("selection-active-bg")},
                 {"selected-text-color", token_ref("selection-active-text")},
                 {"inactive-selected-background", token_ref("selection-inactive-bg")},
                 {"inactive-selected-text-color", token_ref("selection-inactive-text")},
             });

    add_rule(theme, {"info-bar"}, StateFlags::None, {{"close-color", token_ref("text-secondary")}});

    add_rule(theme, {"popover"}, StateFlags::None, {{"background", token_ref("surface-card")}});

    add_rule(theme,
             {"progress-bar"},
             StateFlags::None,
             {
                 {"fill-color", token_ref("accent")},
                 {"track-color", token_ref("scrollbar-track")},
             });

    add_rule(theme,
             {"radio-button"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-field")},
                 {"border-color", token_ref("border-field")},
                 {"selected-color", token_ref("accent")},
             });

    add_rule(theme,
             {"search-field"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-field")},
                 {"border-color", token_ref("border-field")},
                 {"icon-color", token_ref("text-secondary")},
             });
    add_rule(
        theme, {"search-field"}, StateFlags::Focused, {{"border-color", token_ref("focus-ring")}});

    add_rule(theme, {"separator"}, StateFlags::None, {{"color", token_ref("border-subtle")}});

    add_rule(theme,
             {"slider"},
             StateFlags::None,
             {
                 {"fill-color", token_ref("accent")},
                 {"track-background", token_ref("border-subtle")},
                 // Knob stays light in both GNOME schemes, like the switch thumb.
                 {"thumb-color", token_ref("accent-contrast")},
                 {"thumb-border-color", token_ref("border-strong")},
             });

    add_rule(theme,
             {"spin-button"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-field")},
                 {"border-color", token_ref("border-field")},
                 {"button-background", token_ref("surface-hover")},
                 {"armed-background", token_ref("surface-pressed")},
                 {"button-text-color", token_ref("text-secondary")},
             });

    add_rule(theme, {"spinner"}, StateFlags::None, {{"color", token_ref("text-secondary")}});

    add_rule(
        theme, {"split-view"}, StateFlags::None, {{"divider-color", token_ref("border-subtle")}});
    add_rule(theme,
             {"navigation-split-view"},
             StateFlags::None,
             {{"sidebar-background", token_ref("sidebar-bg")},
              {"sidebar-border-color", token_ref("sidebar-border")}});
    add_rule(theme,
             {"overlay-split-view"},
             StateFlags::None,
             {{"sidebar-background", token_ref("sidebar-bg")},
              {"sidebar-border-color", token_ref("sidebar-border")},
              {"scrim-color", StyleValue{Color{0.0F, 0.0F, 0.0F, 0.24F}}}});
    add_rule(theme,
             {"adaptive-scrim"},
             StateFlags::None,
             {{"background", StyleValue{Color{0.0F, 0.0F, 0.0F, 0.24F}}}});
    add_rule(
        theme,
        {"adaptive-sidebar-surface"},
        StateFlags::None,
        {{"background", token_ref("sidebar-bg")}, {"border-color", token_ref("sidebar-border")}});
    add_rule(theme,
             {"preferences-row"},
             StateFlags::None,
             {{"background", token_ref("card-bg")},
              {"border-color", token_ref("border-subtle")},
              {"text-color", token_ref("card-fg")},
              {"subtitle-color", token_ref("text-secondary")},
              {"corner-radius", token_ref("radius-card")}});
    add_rule(theme,
             {"banner"},
             StateFlags::None,
             {{"background", token_ref("accent-soft")},
              {"text-color", token_ref("text-primary")},
              {"action-color", token_ref("accent")},
              {"font-size", token_ref("font-size-body")}});
    add_rule(theme,
             {"status-page"},
             StateFlags::None,
             {{"text-color", token_ref("text-primary")},
              {"description-color", token_ref("text-secondary")},
              {"title-font-size", token_ref("font-size-title")},
              {"body-font-size", token_ref("font-size-body")}});
    add_rule(theme,
             {"toast-overlay"},
             StateFlags::None,
             {{"background", token_ref("surface-osd")},
              {"action-color", token_ref("accent")},
              {"corner-radius", token_ref("radius-popup")},
              {"font-size", token_ref("font-size-body")}});
    add_rule(theme,
             {"toast-surface"},
             StateFlags::None,
             {{"background", token_ref("surface-osd")},
              {"text-color", token_ref("text-osd")},
              {"action-color", token_ref("accent")},
              {"corner-radius", token_ref("radius-popup")},
              {"font-size", token_ref("font-size-body")}});
    add_rule(theme,
             {"preferences-page"},
             StateFlags::None,
             {{"maximum-width", token_ref("clamp-maximum-size")},
              {"page-title-font-size", token_ref("font-size-title")},
              {"description-font-size", token_ref("font-size-body")}});

    add_rule(theme,
             {"switch"},
             StateFlags::None,
             {
                 {"active-track-color", token_ref("accent")},
                 {"inactive-track-color", token_ref("border-strong")},
                 {"thumb-color", token_ref("accent-contrast")},
             });

    add_rule(theme,
             {"tab-bar"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-panel")},
                 {"text-color", token_ref("text-secondary")},
             });

    add_rule(theme,
             {"text-area"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-field")},
                 {"border-color", token_ref("border-field")},
             });
    add_rule(
        theme, {"text-area"}, StateFlags::Focused, {{"border-color", token_ref("focus-ring")}});
    add_rule(theme,
             {"text-area"},
             StateFlags::Disabled,
             {
                 {"background", token_ref("surface-panel")},
                 {"text-color", token_ref("text-disabled")},
             });

    add_rule(theme, {"toolbar"}, StateFlags::None, {{"background", token_ref("surface-panel")}});
    add_rule(theme,
             {"toolbar-view"},
             StateFlags::None,
             {{"bar-background", token_ref("headerbar-bg")},
              {"bar-backdrop-background", token_ref("headerbar-backdrop")},
              {"bar-border-color", token_ref("headerbar-shade")}});
    add_rule(theme,
             {"toolbar-view-backdrop"},
             StateFlags::None,
             {{"bar-background", token_ref("headerbar-bg")},
              {"bar-backdrop-background", token_ref("headerbar-backdrop")},
              {"bar-border-color", token_ref("headerbar-shade")},
              {"separator-width", token_ref("toolbar-view-separator-width")}});
    add_rule(theme,
             {"headerbar"},
             StateFlags::None,
             {{"background", token_ref("headerbar-bg")},
              {"text-color", token_ref("headerbar-fg")},
              {"border-color", token_ref("headerbar-shade")}});
    add_rule(theme,
             {"headerbar-control"},
             StateFlags::None,
             {{"target-size", token_ref("headerbar-control-target")},
              {"control-size", token_ref("headerbar-control-size")},
              {"icon-size", token_ref("headerbar-icon-size")},
              {"symbol-font-size", token_ref("font-size-value")},
              {"close-background", token_ref("surface-hover")},
              {"tooltip-font-size", token_ref("font-size-caption")},
              {"tooltip-background", token_ref("surface-osd")},
              {"tooltip-text", token_ref("text-osd")},
              {"tooltip-radius", token_ref("radius-control")}});

    add_rule(theme,
             {"tooltip"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-osd")},
                 {"text-color", token_ref("text-osd")},
             });

    add_rule(theme,
             {"tree-view"},
             StateFlags::None,
             {
                 {"background", token_ref("surface-card")},
                 {"selected-background", token_ref("selection-active-bg")},
                 {"selected-text-color", token_ref("selection-active-text")},
                 {"inactive-selected-background", token_ref("selection-inactive-bg")},
                 {"inactive-selected-text-color", token_ref("selection-inactive-text")},
             });

    // --- Inactive-window rules: dim text when window loses focus ---
    add_rule(theme,
             {"label", "window-inactive"},
             StateFlags::None,
             {{"text-color", StyleValue{std::string("text-disabled")}}});
    add_rule(theme,
             {"button", "window-inactive"},
             StateFlags::None,
             {{"text-color", StyleValue{std::string("text-disabled")}},
              {"border-color", StyleValue{std::string("border-subtle")}}});
}

// Shell layer roles for the Windows families: route the command and status
// surfaces to their layer tokens so the title-bar/command/navigation/content/
// status vocabulary is actually consumed rather than only declared. Control
// geometry (radii, heights, padding) is no longer overridden per family — each
// family carries its personality in the radius-role and metric tokens instead,
// so the shared rules resolve to native values without duplicated rules.
void install_windows_shell_rules(Theme& theme) {
    add_rule(theme,
             {"menu-bar"},
             StateFlags::None,
             {{"background", StyleValue{std::string("layer-command-bg")}}});
    add_rule(theme,
             {"status-bar"},
             StateFlags::None,
             {{"background", StyleValue{std::string("layer-status-bg")}}});
}

void install_windows_layer_tokens(Theme& theme, bool dark) {
    if (dark) {
        set_color_token(theme, "layer-titlebar-bg", 32, 32, 32);
        set_color_token(theme, "layer-titlebar-text", 255, 255, 255);
        set_color_token(theme, "layer-titlebar-inactive-bg", 32, 32, 32);
        set_color_token(theme, "layer-titlebar-inactive-text", 119, 119, 119);
        set_color_token(theme, "layer-command-bg", 43, 43, 43);
        set_color_token(theme, "layer-navigation-bg", 39, 39, 39);
        set_color_token(theme, "layer-content-bg", 32, 32, 32);
        set_color_token(theme, "layer-status-bg", 32, 32, 32);
    } else {
        set_color_token(theme, "layer-titlebar-bg", 243, 243, 243);
        set_color_token(theme, "layer-titlebar-text", 26, 26, 26);
        set_color_token(theme, "layer-titlebar-inactive-bg", 243, 243, 243);
        set_color_token(theme, "layer-titlebar-inactive-text", 156, 156, 156);
        set_color_token(theme, "layer-command-bg", 251, 251, 251);
        set_color_token(theme, "layer-navigation-bg", 238, 238, 238);
        set_color_token(theme, "layer-content-bg", 255, 255, 255);
        set_color_token(theme, "layer-status-bg", 243, 243, 243);
    }
}

// macOS-family rule overrides, installed after the shared rules so the
// source-order tie-break lets them win at equal specificity. The popup chevron
// renders as an accent capsule inside the control (NSPopUpButton pattern)
// instead of the divided segment the Windows families keep (R4).
void install_macos_overrides(Theme& theme) {
    add_rule(theme,
             {"combo-box"},
             StateFlags::None,
             {
                 {"chevron-style", StyleValue{std::string("capsule")}},
                 {"chevron-color", token_ref("accent-contrast")},
                 {"chevron-background", token_ref("accent")},
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
                                 std::string_view property_name,
                                 int min_specificity) const {

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

        // Use >= so that, on equal specificity, a later rule wins. This is the
        // standard CSS source-order tie-break and lets per-family override rules
        // (installed after the shared rules) take effect.
        const int spec = sel.specificity();
        if (spec < min_specificity) {
            continue;
        }
        if (spec >= best_specificity) {
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
    set_color_token(*theme, "surface-field", 255, 255, 255);
    set_color_token(*theme, "border-field", 203, 209, 217);
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
    // GNOME OSD role: tooltips and transient overlays stay dark in both
    // schemes, matching the libadwaita osd style.
    set_color_token(*theme, "surface-osd", 38, 38, 43, 0.95F);
    set_color_token(*theme, "text-osd", 255, 255, 255);
    install_legacy_token_aliases(*theme);
    install_selection_tokens(*theme, "accent-soft", "text-primary");
    theme->set_token("accent-source", StyleValue{std::string("theme")});
    theme->set_token("motion-mode", StyleValue{std::string("normal")});
    theme->set_token("transparency-mode", StyleValue{std::string("allowed")});
    theme->set_token("density", StyleValue{std::string("standard")});
    // GNOME overlay scrollbars: slim thumb over content, no persistent track.
    theme->set_token("scrollbar-mode", StyleValue{std::string("overlay")});

    set_metric_token(*theme, "spacing-xs", 4.0F);
    set_metric_token(*theme, "spacing-sm", 8.0F);
    set_metric_token(*theme, "spacing-md", 12.0F);
    set_metric_token(*theme, "spacing-lg", 16.0F);
    set_metric_token(*theme, "spacing-xl", 24.0F);
    install_type_scale_tokens(*theme, 18.0F, 13.0F, 12.0F, 17.0F);
    set_metric_token(*theme, "control-height", 36.0F);
    set_metric_token(*theme, "menu-height", 30.0F);
    set_metric_token(*theme, "status-height", 28.0F);
    // GNOME radius roles: generously rounded controls and cards.
    set_metric_token(*theme, "radius-control", 10.0F);
    set_metric_token(*theme, "radius-card", 12.0F);
    set_metric_token(*theme, "radius-popup", 12.0F);
    set_metric_token(*theme, "radius-selection", 8.0F);
    set_metric_token(*theme, "radius-segment", 12.0F);
    set_metric_token(*theme, "radius-image", 14.0F);
    set_metric_token(*theme, "radius-image-content", 12.0F);
    set_metric_token(*theme, "padding-control-x", 16.0F);
    set_metric_token(*theme, "padding-control-y", 9.0F);
    set_metric_token(*theme, "control-min-width", 82.0F);
    set_metric_token(*theme, "segment-track-padding", 4.0F);
    install_shared_metric_tokens(*theme);

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
    set_color_token(*theme, "surface-field", 43, 47, 55);
    set_color_token(*theme, "border-field", 83, 89, 101);
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
    // GNOME OSD role: a step lighter than the dark surfaces so overlays read
    // as elevated, matching the libadwaita osd style.
    set_color_token(*theme, "surface-osd", 58, 58, 64, 0.98F);
    set_color_token(*theme, "text-osd", 255, 255, 255);
    install_legacy_token_aliases(*theme);
    install_selection_tokens(*theme, "accent-soft", "text-primary");
    theme->set_token("accent-source", StyleValue{std::string("theme")});
    theme->set_token("motion-mode", StyleValue{std::string("normal")});
    theme->set_token("transparency-mode", StyleValue{std::string("allowed")});
    theme->set_token("density", StyleValue{std::string("standard")});
    // GNOME overlay scrollbars: slim thumb over content, no persistent track.
    theme->set_token("scrollbar-mode", StyleValue{std::string("overlay")});

    set_metric_token(*theme, "spacing-xs", 4.0F);
    set_metric_token(*theme, "spacing-sm", 8.0F);
    set_metric_token(*theme, "spacing-md", 12.0F);
    set_metric_token(*theme, "spacing-lg", 16.0F);
    set_metric_token(*theme, "spacing-xl", 24.0F);
    install_type_scale_tokens(*theme, 18.0F, 13.0F, 12.0F, 17.0F);
    set_metric_token(*theme, "control-height", 34.0F);
    set_metric_token(*theme, "menu-height", 32.0F);
    set_metric_token(*theme, "status-height", 28.0F);
    // GNOME radius roles: generously rounded controls and cards.
    set_metric_token(*theme, "radius-control", 10.0F);
    set_metric_token(*theme, "radius-card", 12.0F);
    set_metric_token(*theme, "radius-popup", 12.0F);
    set_metric_token(*theme, "radius-selection", 8.0F);
    set_metric_token(*theme, "radius-segment", 12.0F);
    set_metric_token(*theme, "radius-image", 14.0F);
    set_metric_token(*theme, "radius-image-content", 12.0F);
    set_metric_token(*theme, "padding-control-x", 16.0F);
    set_metric_token(*theme, "padding-control-y", 9.0F);
    set_metric_token(*theme, "control-min-width", 82.0F);
    set_metric_token(*theme, "segment-track-padding", 4.0F);
    install_shared_metric_tokens(*theme);

    install_shared_rules(*theme);

    return theme;
}

std::unique_ptr<Theme> Theme::make_linux_gnome(ColorScheme color_scheme) {
    auto theme = color_scheme == ColorScheme::Dark ? Theme::make_dark() : Theme::make_light();
    theme->set_token("theme-family", StyleValue{std::string("linux-gnome")});
    return theme;
}

std::unique_ptr<Theme> Theme::make_windows_11(ColorScheme color_scheme) {
    const bool dark = color_scheme == ColorScheme::Dark;
    auto theme = std::make_unique<Theme>(dark ? "Windows 11 Dark" : "Windows 11 Light");
    theme->set_token("theme-family", StyleValue{std::string("windows-11")});

    if (dark) {
        set_color_token(*theme, "window-bg", 32, 32, 32);
        set_color_token(*theme, "surface-panel", 43, 43, 43);
        set_color_token(*theme, "surface-card", 44, 44, 44);
        set_color_token(*theme, "surface-raised", 50, 50, 50);
        set_color_token(*theme, "surface-hover", 55, 55, 55);
        set_color_token(*theme, "surface-pressed", 62, 62, 62);
        set_color_token(*theme, "surface-field", 45, 45, 45);
        set_color_token(*theme, "border-field", 76, 76, 76);
        set_color_token(*theme, "border-subtle", 60, 60, 60);
        set_color_token(*theme, "border-strong", 90, 90, 90);
        set_color_token(*theme, "text-primary", 255, 255, 255);
        set_color_token(*theme, "text-secondary", 180, 180, 180);
        set_color_token(*theme, "text-disabled", 119, 119, 119);
        set_color_token(*theme, "accent", 96, 205, 255);
        set_color_token(*theme, "accent-hover", 120, 215, 255);
        set_color_token(*theme, "accent-pressed", 80, 180, 235);
        set_color_token(*theme, "accent-soft", 38, 80, 110);
        set_color_token(*theme, "accent-contrast", 0, 0, 0);
        set_color_token(*theme, "focus-ring", 96, 205, 255);
        set_color_token(*theme, "focus-visible", 255, 255, 255);
        set_color_token(*theme, "scrollbar-track", 46, 46, 46);
        set_color_token(*theme, "scrollbar-thumb", 119, 119, 119);
        // Fluent tooltips follow the theme scheme.
        set_color_token(*theme, "surface-osd", 44, 44, 44);
        set_color_token(*theme, "text-osd", 255, 255, 255);
    } else {
        set_color_token(*theme, "window-bg", 243, 243, 243);
        set_color_token(*theme, "surface-panel", 238, 238, 238);
        set_color_token(*theme, "surface-card", 251, 251, 251);
        set_color_token(*theme, "surface-raised", 255, 255, 255);
        set_color_token(*theme, "surface-hover", 234, 234, 234);
        set_color_token(*theme, "surface-pressed", 226, 226, 226);
        set_color_token(*theme, "surface-field", 255, 255, 255);
        set_color_token(*theme, "border-field", 209, 209, 209);
        set_color_token(*theme, "border-subtle", 229, 229, 229);
        set_color_token(*theme, "border-strong", 200, 200, 200);
        set_color_token(*theme, "text-primary", 26, 26, 26);
        set_color_token(*theme, "text-secondary", 95, 95, 95);
        set_color_token(*theme, "text-disabled", 156, 156, 156);
        set_color_token(*theme, "accent", 0, 103, 192);
        set_color_token(*theme, "accent-hover", 0, 90, 170);
        set_color_token(*theme, "accent-pressed", 0, 78, 148);
        set_color_token(*theme, "accent-soft", 219, 233, 247);
        set_color_token(*theme, "accent-contrast", 255, 255, 255);
        set_color_token(*theme, "focus-ring", 0, 103, 192);
        set_color_token(*theme, "focus-visible", 26, 26, 26);
        set_color_token(*theme, "scrollbar-track", 236, 236, 236);
        set_color_token(*theme, "scrollbar-thumb", 195, 195, 195);
        // Fluent tooltips follow the theme scheme.
        set_color_token(*theme, "surface-osd", 249, 249, 249);
        set_color_token(*theme, "text-osd", 26, 26, 26);
    }

    install_windows_layer_tokens(*theme, dark);
    install_legacy_token_aliases(*theme);
    install_selection_tokens(*theme, "accent-soft", "text-primary");
    // Windows keeps persistent scrollbars — the native desktop convention.
    theme->set_token("scrollbar-mode", StyleValue{std::string("persistent")});

    theme->set_token("accent-source", StyleValue{std::string("theme")});
    theme->set_token("motion-mode", StyleValue{std::string("normal")});
    theme->set_token("transparency-mode", StyleValue{std::string("allowed")});
    theme->set_token("density", StyleValue{std::string("standard")});

    set_metric_token(*theme, "spacing-xs", 4.0F);
    set_metric_token(*theme, "spacing-sm", 8.0F);
    set_metric_token(*theme, "spacing-md", 12.0F);
    set_metric_token(*theme, "spacing-lg", 16.0F);
    set_metric_token(*theme, "spacing-xl", 24.0F);
    install_type_scale_tokens(*theme, 18.0F, 13.0F, 12.0F, 17.0F);
    set_metric_token(*theme, "control-height", 32.0F);
    set_metric_token(*theme, "menu-height", 32.0F);
    set_metric_token(*theme, "status-height", 26.0F);
    // Windows 11 / WinUI radius roles: 4 px control corners, rounded surface
    // cards and popups — deliberately distinct from the GNOME defaults so the
    // Windows family no longer clones Linux.
    set_metric_token(*theme, "radius-control", 4.0F);
    set_metric_token(*theme, "radius-card", 8.0F);
    set_metric_token(*theme, "radius-popup", 8.0F);
    set_metric_token(*theme, "radius-selection", 4.0F);
    set_metric_token(*theme, "radius-segment", 6.0F);
    set_metric_token(*theme, "radius-image", 8.0F);
    set_metric_token(*theme, "radius-image-content", 6.0F);
    set_metric_token(*theme, "padding-control-x", 12.0F);
    set_metric_token(*theme, "padding-control-y", 5.0F);
    set_metric_token(*theme, "control-min-width", 90.0F);
    set_metric_token(*theme, "segment-track-padding", 2.0F);
    install_shared_metric_tokens(*theme);

    install_shared_rules(*theme);
    install_windows_shell_rules(*theme);

    return theme;
}

std::unique_ptr<Theme> Theme::make_windows_10(ColorScheme color_scheme) {
    const bool dark = color_scheme == ColorScheme::Dark;
    auto theme = std::make_unique<Theme>(dark ? "Windows 10 Dark" : "Windows 10 Light");
    theme->set_token("theme-family", StyleValue{std::string("windows-10")});

    if (dark) {
        set_color_token(*theme, "window-bg", 31, 31, 31);
        set_color_token(*theme, "surface-panel", 44, 44, 44);
        set_color_token(*theme, "surface-card", 43, 43, 43);
        set_color_token(*theme, "surface-raised", 51, 51, 51);
        set_color_token(*theme, "surface-hover", 60, 60, 60);
        set_color_token(*theme, "surface-pressed", 68, 68, 68);
        set_color_token(*theme, "surface-field", 45, 45, 45);
        set_color_token(*theme, "border-field", 100, 100, 100);
        set_color_token(*theme, "border-subtle", 61, 61, 61);
        set_color_token(*theme, "border-strong", 92, 92, 92);
        set_color_token(*theme, "text-primary", 255, 255, 255);
        set_color_token(*theme, "text-secondary", 180, 180, 180);
        set_color_token(*theme, "text-disabled", 122, 122, 122);
        set_color_token(*theme, "accent", 0, 120, 215);
        set_color_token(*theme, "accent-hover", 38, 140, 225);
        set_color_token(*theme, "accent-pressed", 0, 103, 184);
        set_color_token(*theme, "accent-soft", 0, 55, 100);
        set_color_token(*theme, "accent-contrast", 255, 255, 255);
        set_color_token(*theme, "focus-ring", 76, 194, 255);
        set_color_token(*theme, "focus-visible", 255, 255, 255);
        set_color_token(*theme, "scrollbar-track", 46, 46, 46);
        set_color_token(*theme, "scrollbar-thumb", 104, 104, 104);
        // Classic tooltips follow the theme scheme.
        set_color_token(*theme, "surface-osd", 43, 43, 43);
        set_color_token(*theme, "text-osd", 255, 255, 255);
    } else {
        set_color_token(*theme, "window-bg", 255, 255, 255);
        set_color_token(*theme, "surface-panel", 242, 242, 242);
        set_color_token(*theme, "surface-card", 255, 255, 255);
        set_color_token(*theme, "surface-raised", 251, 251, 251);
        set_color_token(*theme, "surface-hover", 240, 240, 240);
        set_color_token(*theme, "surface-pressed", 224, 224, 224);
        set_color_token(*theme, "surface-field", 255, 255, 255);
        set_color_token(*theme, "border-field", 138, 138, 138);
        set_color_token(*theme, "border-subtle", 213, 213, 213);
        set_color_token(*theme, "border-strong", 160, 160, 160);
        set_color_token(*theme, "text-primary", 0, 0, 0);
        set_color_token(*theme, "text-secondary", 96, 96, 96);
        set_color_token(*theme, "text-disabled", 160, 160, 160);
        set_color_token(*theme, "accent", 0, 120, 215);
        set_color_token(*theme, "accent-hover", 0, 108, 195);
        set_color_token(*theme, "accent-pressed", 0, 96, 170);
        set_color_token(*theme, "accent-soft", 205, 232, 255);
        set_color_token(*theme, "accent-contrast", 255, 255, 255);
        set_color_token(*theme, "focus-ring", 0, 120, 215);
        set_color_token(*theme, "focus-visible", 0, 0, 0);
        set_color_token(*theme, "scrollbar-track", 240, 240, 240);
        set_color_token(*theme, "scrollbar-thumb", 205, 205, 205);
        // Classic tooltips follow the theme scheme.
        set_color_token(*theme, "surface-osd", 255, 255, 255);
        set_color_token(*theme, "text-osd", 0, 0, 0);
    }

    install_windows_layer_tokens(*theme, dark);
    install_legacy_token_aliases(*theme);
    install_selection_tokens(*theme, "accent-soft", "text-primary");
    // Windows keeps persistent scrollbars — the native desktop convention.
    theme->set_token("scrollbar-mode", StyleValue{std::string("persistent")});

    theme->set_token("accent-source", StyleValue{std::string("theme")});
    theme->set_token("motion-mode", StyleValue{std::string("normal")});
    theme->set_token("transparency-mode", StyleValue{std::string("allowed")});
    theme->set_token("density", StyleValue{std::string("standard")});

    set_metric_token(*theme, "spacing-xs", 4.0F);
    set_metric_token(*theme, "spacing-sm", 8.0F);
    set_metric_token(*theme, "spacing-md", 12.0F);
    set_metric_token(*theme, "spacing-lg", 16.0F);
    set_metric_token(*theme, "spacing-xl", 24.0F);
    install_type_scale_tokens(*theme, 18.0F, 13.0F, 12.0F, 17.0F);
    set_metric_token(*theme, "control-height", 32.0F);
    set_metric_token(*theme, "menu-height", 28.0F);
    set_metric_token(*theme, "status-height", 24.0F);
    // Windows 10 radius roles: the near-square classic geometry, distinct from
    // the rounded Windows 11 family.
    set_metric_token(*theme, "radius-control", 2.0F);
    set_metric_token(*theme, "radius-card", 2.0F);
    set_metric_token(*theme, "radius-popup", 2.0F);
    set_metric_token(*theme, "radius-selection", 0.0F);
    set_metric_token(*theme, "radius-segment", 2.0F);
    set_metric_token(*theme, "radius-image", 2.0F);
    set_metric_token(*theme, "radius-image-content", 0.0F);
    set_metric_token(*theme, "padding-control-x", 12.0F);
    set_metric_token(*theme, "padding-control-y", 5.0F);
    set_metric_token(*theme, "control-min-width", 90.0F);
    set_metric_token(*theme, "segment-track-padding", 2.0F);
    install_shared_metric_tokens(*theme);

    install_shared_rules(*theme);
    install_windows_shell_rules(*theme);

    return theme;
}

std::unique_ptr<Theme> Theme::make_macos_26(ColorScheme color_scheme) {
    const bool dark = color_scheme == ColorScheme::Dark;
    auto theme = std::make_unique<Theme>(dark ? "macOS Dark" : "macOS Light");
    theme->set_token("theme-family", StyleValue{std::string("macos-26")});

    if (dark) {
        set_color_token(*theme, "window-bg", 30, 30, 30);
        set_color_token(*theme, "surface-panel", 38, 38, 38);
        set_color_token(*theme, "surface-card", 44, 44, 44);
        set_color_token(*theme, "surface-raised", 52, 52, 52);
        set_color_token(*theme, "surface-hover", 62, 62, 62);
        set_color_token(*theme, "surface-pressed", 72, 72, 72);
        set_color_token(*theme, "surface-field", 38, 38, 38);
        set_color_token(*theme, "border-field", 70, 70, 70);
        set_color_token(*theme, "border-subtle", 60, 60, 60);
        set_color_token(*theme, "border-strong", 85, 85, 85);
        set_color_token(*theme, "text-primary", 255, 255, 255);
        set_color_token(*theme, "text-secondary", 170, 170, 170);
        set_color_token(*theme, "text-disabled", 100, 100, 100);
        set_color_token(*theme, "accent", 10, 132, 255);
        set_color_token(*theme, "accent-hover", 30, 145, 255);
        set_color_token(*theme, "accent-pressed", 0, 118, 240);
        set_color_token(*theme, "accent-soft", 25, 55, 95);
        set_color_token(*theme, "accent-contrast", 255, 255, 255);
        set_color_token(*theme, "focus-ring", 10, 132, 255);
        set_color_token(*theme, "scrollbar-track", 50, 50, 50);
        set_color_token(*theme, "scrollbar-thumb", 100, 100, 100);
        // AppKit tooltips follow the theme scheme.
        set_color_token(*theme, "surface-osd", 58, 58, 58, 0.98F);
        set_color_token(*theme, "text-osd", 255, 255, 255);
    } else {
        set_color_token(*theme, "window-bg", 246, 246, 246);
        set_color_token(*theme, "surface-panel", 244, 244, 244);
        set_color_token(*theme, "surface-card", 255, 255, 255);
        set_color_token(*theme, "surface-raised", 255, 255, 255);
        set_color_token(*theme, "surface-hover", 232, 232, 232);
        set_color_token(*theme, "surface-pressed", 220, 220, 220);
        set_color_token(*theme, "surface-field", 255, 255, 255);
        set_color_token(*theme, "border-field", 210, 210, 210);
        set_color_token(*theme, "border-subtle", 218, 218, 218);
        set_color_token(*theme, "border-strong", 190, 190, 190);
        set_color_token(*theme, "text-primary", 0, 0, 0);
        set_color_token(*theme, "text-secondary", 100, 100, 100);
        set_color_token(*theme, "text-disabled", 160, 160, 160);
        set_color_token(*theme, "accent", 0, 122, 255);
        set_color_token(*theme, "accent-hover", 0, 112, 240);
        set_color_token(*theme, "accent-pressed", 0, 100, 215);
        set_color_token(*theme, "accent-soft", 215, 233, 255);
        set_color_token(*theme, "accent-contrast", 255, 255, 255);
        set_color_token(*theme, "focus-ring", 0, 122, 255);
        set_color_token(*theme, "scrollbar-track", 230, 230, 230);
        set_color_token(*theme, "scrollbar-thumb", 170, 170, 170);
        // AppKit tooltips follow the theme scheme.
        set_color_token(*theme, "surface-osd", 246, 246, 246, 0.98F);
        set_color_token(*theme, "text-osd", 0, 0, 0);
    }

    install_legacy_token_aliases(*theme);
    // macOS active selection is the solid accent with contrast text; the soft
    // accent wash is the GNOME/Windows pattern (R2).
    install_selection_tokens(*theme, "accent", "accent-contrast");
    theme->set_token("accent-source", StyleValue{std::string("theme")});
    theme->set_token("motion-mode", StyleValue{std::string("normal")});
    theme->set_token("transparency-mode", StyleValue{std::string("allowed")});
    theme->set_token("density", StyleValue{std::string("standard")});
    // macOS overlay scrollbars: slim thumb over content, no persistent track.
    theme->set_token("scrollbar-mode", StyleValue{std::string("overlay")});

    set_metric_token(*theme, "spacing-xs", 4.0F);
    set_metric_token(*theme, "spacing-sm", 8.0F);
    set_metric_token(*theme, "spacing-md", 10.0F);
    set_metric_token(*theme, "spacing-lg", 14.0F);
    set_metric_token(*theme, "spacing-xl", 20.0F);
    install_type_scale_tokens(*theme, 17.0F, 13.0F, 11.0F, 16.0F);
    set_metric_token(*theme, "control-height", 28.0F);
    set_metric_token(*theme, "menu-height", 28.0F);
    set_metric_token(*theme, "status-height", 24.0F);
    // macOS radius roles: compact AppKit-style control rounding with softer
    // cards and popups.
    set_metric_token(*theme, "radius-control", 6.0F);
    set_metric_token(*theme, "radius-card", 10.0F);
    set_metric_token(*theme, "radius-popup", 10.0F);
    set_metric_token(*theme, "radius-selection", 6.0F);
    set_metric_token(*theme, "radius-segment", 8.0F);
    set_metric_token(*theme, "radius-image", 10.0F);
    set_metric_token(*theme, "radius-image-content", 8.0F);
    set_metric_token(*theme, "padding-control-x", 12.0F);
    set_metric_token(*theme, "padding-control-y", 4.0F);
    set_metric_token(*theme, "control-min-width", 82.0F);
    set_metric_token(*theme, "segment-track-padding", 2.0F);
    install_shared_metric_tokens(*theme);

    install_shared_rules(*theme);
    install_macos_overrides(*theme);

    return theme;
}

} // namespace nk
