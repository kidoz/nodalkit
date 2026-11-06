#include <algorithm>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/shortcut_label.h>

namespace nk {

namespace {

FontDescriptor shortcut_font() {
    return FontDescriptor{.family = {}, .size = 12.0F, .weight = FontWeight::Medium};
}

std::string key_label(KeyCode key) {
    const auto value = static_cast<int>(key);
    const auto letter = static_cast<int>(KeyCode::A);
    if (value >= letter && value <= static_cast<int>(KeyCode::Z)) {
        return std::string(1, static_cast<char>('A' + (value - letter)));
    }
    const auto digit = static_cast<int>(KeyCode::Num1);
    if (value >= digit && value <= static_cast<int>(KeyCode::Num0)) {
        const int offset = value - digit;
        return std::string(1, offset == 9 ? '0' : static_cast<char>('1' + offset));
    }
    const auto function = static_cast<int>(KeyCode::F1);
    if (value >= function && value <= static_cast<int>(KeyCode::F12)) {
        return "F" + std::to_string(value - function + 1);
    }
    switch (key) {
    case KeyCode::Return:
        return "Return";
    case KeyCode::Escape:
        return "Esc";
    case KeyCode::Tab:
        return "Tab";
    case KeyCode::Space:
        return "Space";
    case KeyCode::Delete:
        return "Delete";
    case KeyCode::Left:
        return "Left";
    case KeyCode::Right:
        return "Right";
    case KeyCode::Up:
        return "Up";
    case KeyCode::Down:
        return "Down";
    default:
        return {};
    }
}

} // namespace

struct ShortcutLabel::Impl {
    NativeMenuShortcut shortcut{};
};

std::string ShortcutLabel::format(const NativeMenuShortcut& shortcut) {
    if (shortcut.key == KeyCode::Unknown) {
        return {};
    }
    const auto key = key_label(shortcut.key);
    if (key.empty()) {
        return {};
    }
    std::string text;
    auto append = [&text](std::string_view part) {
        if (!text.empty()) {
            text += '+';
        }
        text += part;
    };
    const auto modifiers = shortcut.modifiers;
    if ((modifiers & NativeMenuModifier::Ctrl) != NativeMenuModifier::None) {
        append("Ctrl");
    }
    if ((modifiers & NativeMenuModifier::Alt) != NativeMenuModifier::None) {
        append("Alt");
    }
    if ((modifiers & NativeMenuModifier::Shift) != NativeMenuModifier::None) {
        append("Shift");
    }
    if ((modifiers & NativeMenuModifier::Super) != NativeMenuModifier::None) {
        append("Super");
    }
    append(key);
    return text;
}

std::shared_ptr<ShortcutLabel> ShortcutLabel::create(NativeMenuShortcut shortcut) {
    return std::shared_ptr<ShortcutLabel>(new ShortcutLabel(shortcut));
}

ShortcutLabel::ShortcutLabel(NativeMenuShortcut shortcut) : impl_(std::make_unique<Impl>()) {
    impl_->shortcut = shortcut;
    add_style_class("shortcut-label");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    const auto text = format(shortcut);
    accessible.set_name(text);
    accessible.set_hidden(text.empty());
}

ShortcutLabel::~ShortcutLabel() = default;

NativeMenuShortcut ShortcutLabel::shortcut() const {
    return impl_->shortcut;
}

void ShortcutLabel::set_shortcut(NativeMenuShortcut shortcut) {
    if (impl_->shortcut.key == shortcut.key && impl_->shortcut.modifiers == shortcut.modifiers) {
        return;
    }
    impl_->shortcut = shortcut;
    const auto text = format(shortcut);
    ensure_accessible().set_name(text);
    ensure_accessible().set_hidden(text.empty());
    queue_layout();
    queue_redraw();
}

SizeRequest ShortcutLabel::measure(const Constraints& /*constraints*/) const {
    const auto text = format(impl_->shortcut);
    if (text.empty()) {
        return {};
    }
    const auto measured = measure_text(text, shortcut_font());
    return {measured.width, measured.height, measured.width, measured.height};
}

void ShortcutLabel::snapshot(SnapshotContext& ctx) const {
    const auto text = format(impl_->shortcut);
    if (text.empty()) {
        return;
    }
    const auto bounds = allocation();
    const auto font = shortcut_font();
    const auto measured = measure_text(text, font);
    const float text_y = bounds.y + std::max(0.0F, (bounds.height - measured.height) * 0.5F);
    ctx.add_text({bounds.x, text_y}, text, theme_color("text-color"), font);
}

} // namespace nk
