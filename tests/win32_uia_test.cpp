/// @file win32_uia_test.cpp
/// @brief Windows-only UI Automation provider tests driven through the
/// IUIAutomation client API.
///
/// Each case builds a real (realized, possibly shown) NodalKit window, asks
/// the UIA client API for its element tree, and asserts tree shape, control
/// types, names, patterns, and geometry against the shared accessibility
/// model. The client runs on the same STA thread as the providers, so calls
/// are direct and need no message pumping. Tagged `[windows]`; registered
/// non-parallel because native windows need exclusive desktop interaction.

#if defined(_WIN32)

#include "../src/accessibility/accessibility_tree.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/platform/windows_interop.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/check_box.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/label.h>
#include <nk/widgets/progress_bar.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/slider.h>
#include <nk/widgets/spin_button.h>
#include <nk/widgets/tab_bar.h>
#include <nk/widgets/text_field.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// objbase.h defines the `interface` keyword the UI Automation headers use;
// WIN32_LEAN_AND_MEAN strips it out of windows.h.
#include <objbase.h>
#include <uiautomation.h>
#include <windows.h>
#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;

/// Catch2 v3 has no HRESULT assertion; centralize the check instead of a macro.
void require_ok(HRESULT hr) {
    REQUIRE(SUCCEEDED(hr));
}

/// Vertical box laying the probe widgets out top to bottom. A role-less box is
/// flattened by the accessibility tree, so its children surface directly under
/// the window root; using the toolkit's real container (not a bare widget)
/// keeps allocations well-formed during frame rendering.
[[nodiscard]] std::shared_ptr<nk::Box> probe_box() {
    return nk::Box::create(nk::Orientation::Vertical, 8.0F);
}

class UiaClient {
public:
    UiaClient() {
        ComPtr<IUIAutomation> automation;
        const HRESULT hr = CoCreateInstance(
            CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, &automation);
        if (FAILED(hr)) {
            FAIL("CoCreateInstance(CUIAutomation) failed: " << std::hex << hr);
        }
        automation_ = std::move(automation);
    }

    [[nodiscard]] IUIAutomation* get() const { return automation_.Get(); }

    /// Find a descendant by control type, searching from the window element.
    [[nodiscard]] ComPtr<IUIAutomationElement> find_by_control_type(IUIAutomationElement* scope,
                                                                    CONTROLTYPEID type) {
        VARIANT condition_value{};
        condition_value.vt = VT_I4;
        condition_value.lVal = static_cast<LONG>(type);
        ComPtr<IUIAutomationCondition> condition;
        ComPtr<IUIAutomationElement> found;
        if (SUCCEEDED(automation_->CreatePropertyCondition(
                UIA_ControlTypePropertyId, condition_value, &condition))) {
            (void)scope->FindFirst(TreeScope_Descendants, condition.Get(), &found);
        }
        VariantClear(&condition_value);
        return found;
    }

    /// Find a descendant by name, searching from the window element.
    [[nodiscard]] ComPtr<IUIAutomationElement> find_by_name(IUIAutomationElement* scope,
                                                            const char* name_utf8) {
        const int wide_length = MultiByteToWideChar(
            CP_UTF8, 0, name_utf8, static_cast<int>(std::strlen(name_utf8)), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
        MultiByteToWideChar(CP_UTF8,
                            0,
                            name_utf8,
                            static_cast<int>(std::strlen(name_utf8)),
                            wide.data(),
                            wide_length);

        VARIANT condition_value{};
        condition_value.vt = VT_BSTR;
        condition_value.bstrVal = SysAllocStringLen(wide.c_str(), static_cast<UINT>(wide.size()));
        ComPtr<IUIAutomationCondition> condition;
        ComPtr<IUIAutomationElement> found;
        if (SUCCEEDED(automation_->CreatePropertyCondition(
                UIA_NamePropertyId, condition_value, &condition))) {
            (void)scope->FindFirst(TreeScope_Descendants, condition.Get(), &found);
        }
        VariantClear(&condition_value);
        return found;
    }

    [[nodiscard]] std::wstring string_property(IUIAutomationElement* element, PROPERTYID id) {
        VARIANT value{};
        if (FAILED(element->GetCurrentPropertyValue(id, &value)) || value.vt != VT_BSTR) {
            VariantClear(&value);
            return {};
        }
        std::wstring result(value.bstrVal != nullptr ? value.bstrVal : L"");
        VariantClear(&value);
        return result;
    }

    [[nodiscard]] int int_property(IUIAutomationElement* element, PROPERTYID id) {
        VARIANT value{};
        if (FAILED(element->GetCurrentPropertyValue(id, &value)) || value.vt != VT_I4) {
            VariantClear(&value);
            return 0;
        }
        const int result = value.lVal;
        VariantClear(&value);
        return result;
    }

    [[nodiscard]] bool bool_property(IUIAutomationElement* element, PROPERTYID id) {
        VARIANT value{};
        if (FAILED(element->GetCurrentPropertyValue(id, &value)) || value.vt != VT_BOOL) {
            VariantClear(&value);
            return false;
        }
        const bool result = value.boolVal != VARIANT_FALSE;
        VariantClear(&value);
        return result;
    }

private:
    ComPtr<IUIAutomation> automation_;
};

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        text.c_str(),
                        static_cast<int>(text.size()),
                        result.data(),
                        length,
                        nullptr,
                        nullptr);
    return result;
}

} // namespace

TEST_CASE("Win32 UIA exposes the widget tree with control types", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA tree probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto button = nk::Button::create("Activate me");
    auto check = nk::CheckBox::create("Enable extras");
    auto label = nk::Label::create("Just text");
    root->append(button);
    root->append(check);
    root->append(label);
    window.set_child(root);
    window.present();

    const HWND hwnd = nk::window_hwnd(window);
    REQUIRE(hwnd != nullptr);

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(hwnd, &element));
    REQUIRE(element != nullptr);

    // The root describes itself as a NodalKit window.
    CHECK(uia.int_property(element.Get(), UIA_ControlTypePropertyId) == UIA_WindowControlTypeId);
    CHECK(narrow(uia.string_property(element.Get(), UIA_FrameworkIdPropertyId)) == "NodalKit");

    // Walk the control view. UIA appends host-provided chrome (title bar)
    // after the fragment children, so collect the NodalKit children in order.
    ComPtr<IUIAutomationTreeWalker> walker;
    require_ok(uia.get()->get_ControlViewWalker(&walker));

    std::vector<std::pair<std::string, int>> toolkit_children;
    ComPtr<IUIAutomationElement> current;
    require_ok(walker->GetFirstChildElement(element.Get(), &current));
    while (current != nullptr) {
        if (narrow(uia.string_property(current.Get(), UIA_FrameworkIdPropertyId)) == "NodalKit") {
            toolkit_children.emplace_back(
                narrow(uia.string_property(current.Get(), UIA_NamePropertyId)),
                uia.int_property(current.Get(), UIA_ControlTypePropertyId));
        }
        ComPtr<IUIAutomationElement> next;
        require_ok(walker->GetNextSiblingElement(current.Get(), &next));
        current = std::move(next);
    }

    REQUIRE(toolkit_children.size() == 3);
    CHECK(toolkit_children[0] ==
          std::make_pair(std::string("Activate me"), static_cast<int>(UIA_ButtonControlTypeId)));
    CHECK(toolkit_children[1] == std::make_pair(std::string("Enable extras"),
                                                static_cast<int>(UIA_CheckBoxControlTypeId)));
    CHECK(toolkit_children[2] ==
          std::make_pair(std::string("Just text"), static_cast<int>(UIA_TextControlTypeId)));

    // Walking up from a widget lands on the NodalKit window root.
    ComPtr<IUIAutomationElement> first_widget;
    require_ok(walker->GetFirstChildElement(element.Get(), &first_widget));
    ComPtr<IUIAutomationElement> parent;
    require_ok(walker->GetParentElement(first_widget.Get(), &parent));
    REQUIRE(parent != nullptr);
    CHECK(narrow(uia.string_property(parent.Get(), UIA_FrameworkIdPropertyId)) == "NodalKit");
    CHECK(uia.int_property(parent.Get(), UIA_ControlTypePropertyId) == UIA_WindowControlTypeId);
}

TEST_CASE("Win32 UIA patterns drive toolkit actions", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA pattern probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto button = nk::Button::create("Activate me");
    int clicked = 0;
    (void)button->on_clicked().connect([&] { ++clicked; });
    auto check = nk::CheckBox::create("Enable extras");
    root->append(button);
    root->append(check);
    window.set_child(root);
    window.present();

    const HWND hwnd = nk::window_hwnd(window);
    REQUIRE(hwnd != nullptr);

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(hwnd, &element));

    // Invoke on the button fires the toolkit action on the UI thread.
    ComPtr<IUIAutomationElement> button_element = uia.find_by_name(element.Get(), "Activate me");
    REQUIRE(button_element != nullptr);
    ComPtr<IUnknown> invoke_unknown;
    require_ok(
        button_element->GetCurrentPattern(UIA_InvokePatternId, invoke_unknown.GetAddressOf()));
    ComPtr<IUIAutomationInvokePattern> invoke;
    require_ok(invoke_unknown.As(&invoke));
    REQUIRE(invoke != nullptr);
    CHECK(SUCCEEDED(invoke->Invoke()));
    CHECK(clicked == 1);

    // Toggle reflects and changes the checked state.
    ComPtr<IUIAutomationElement> check_element = uia.find_by_name(element.Get(), "Enable extras");
    REQUIRE(check_element != nullptr);
    ComPtr<IUnknown> toggle_unknown;
    require_ok(
        check_element->GetCurrentPattern(UIA_TogglePatternId, toggle_unknown.GetAddressOf()));
    ComPtr<IUIAutomationTogglePattern> toggle;
    require_ok(toggle_unknown.As(&toggle));
    REQUIRE(toggle != nullptr);
    ToggleState state = ToggleState_Indeterminate;
    require_ok(toggle->get_CurrentToggleState(&state));
    CHECK(state == ToggleState_Off);
    require_ok(toggle->Toggle());
    require_ok(toggle->get_CurrentToggleState(&state));
    CHECK(state == ToggleState_On);
}

TEST_CASE("Win32 UIA geometry and automation ids match the model", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA geometry probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto button = nk::Button::create("Activate me");
    root->append(button);
    window.set_child(root);
    window.present();

    const HWND hwnd = nk::window_hwnd(window);
    REQUIRE(hwnd != nullptr);

    // Pump the event loop so the presented window lays out and renders a frame.
    REQUIRE(app.event_loop().poll());

    // Expected values straight from the shared model.
    nk::detail::AccessibilityTree tree(window);
    std::optional<nk::detail::AccessibleId> button_id;
    for (const auto id : tree.root_children()) {
        if (tree.info(id)->name == "Activate me") {
            button_id = id;
        }
    }
    REQUIRE(button_id.has_value());
    const auto model = tree.info(*button_id);
    REQUIRE(model.has_value());
    // Precondition: layout ran, so geometry assertions cannot pass on zeros.
    REQUIRE(model->bounds.width > 0.0F);
    REQUIRE(model->bounds.height > 0.0F);

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(hwnd, &element));
    ComPtr<IUIAutomationElement> button_element = uia.find_by_name(element.Get(), "Activate me");
    REQUIRE(button_element != nullptr);

    // AutomationId is the widget debug name the model reports.
    CHECK(narrow(uia.string_property(button_element.Get(), UIA_AutomationIdPropertyId)) ==
          model->debug_name);

    // BoundingRectangle is the allocation converted to physical screen px.
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    UINT dpi = 96;
    if (const auto get_dpi = reinterpret_cast<UINT (*)(HWND)>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"))) {
        dpi = get_dpi(hwnd);
    }
    const float scale = static_cast<float>(dpi) / 96.0F;
    RECT rect{};
    REQUIRE(button_element->get_CurrentBoundingRectangle(&rect) == S_OK);
    INFO("rect={" << rect.left << "," << rect.top << "," << rect.right << "," << rect.bottom
                  << "} origin={" << origin.x << "," << origin.y << "} scale=" << scale
                  << " bounds={" << model->bounds.x << "," << model->bounds.y << ","
                  << model->bounds.width << "," << model->bounds.height << "}");
    CHECK(std::abs(static_cast<double>(rect.left) - (origin.x + model->bounds.x * scale)) < 1.5);
    CHECK(std::abs(static_cast<double>(rect.top) - (origin.y + model->bounds.y * scale)) < 1.5);
    CHECK(std::abs(static_cast<double>(rect.right - rect.left) - (model->bounds.width * scale)) <
          1.5);
    CHECK(std::abs(static_cast<double>(rect.bottom - rect.top) - (model->bounds.height * scale)) <
          1.5);

    // Enabled/focusable mirror the model.
    CHECK(uia.bool_property(button_element.Get(), UIA_IsEnabledPropertyId) == model->enabled);
    CHECK(uia.bool_property(button_element.Get(), UIA_IsKeyboardFocusablePropertyId) ==
          model->focusable);
}

TEST_CASE("Win32 UIA exposes real numeric ranges", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA range probe", .width = 360, .height = 280});
    auto root = probe_box();
    auto slider = nk::Slider::create();
    slider->set_range(0.0, 100.0);
    slider->set_step(5.0);
    slider->set_value(40.0);
    auto progress = nk::ProgressBar::create();
    progress->set_fraction(0.25F);
    auto spin = nk::SpinButton::create();
    spin->set_range(0.0, 10.0);
    spin->set_step(2.0);
    spin->set_value(2.0);
    root->append(slider);
    root->append(progress);
    root->append(spin);
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(nk::window_hwnd(window), &element));

    SECTION("slider range reads and writes") {
        auto slider_element = uia.find_by_control_type(element.Get(), UIA_SliderControlTypeId);
        REQUIRE(slider_element != nullptr);
        ComPtr<IUnknown> unknown;
        require_ok(
            slider_element->GetCurrentPattern(UIA_RangeValuePatternId, unknown.GetAddressOf()));
        ComPtr<IUIAutomationRangeValuePattern> range;
        require_ok(unknown.As(&range));
        double value = -1.0;
        double minimum = -1.0;
        double maximum = -1.0;
        double small_change = -1.0;
        BOOL read_only = TRUE;
        require_ok(range->get_CurrentValue(&value));
        require_ok(range->get_CurrentMinimum(&minimum));
        require_ok(range->get_CurrentMaximum(&maximum));
        require_ok(range->get_CurrentSmallChange(&small_change));
        require_ok(range->get_CurrentIsReadOnly(&read_only));
        CHECK(value == 40.0);
        CHECK(minimum == 0.0);
        CHECK(maximum == 100.0);
        CHECK(small_change == 5.0);
        CHECK(read_only == FALSE);

        require_ok(range->SetValue(60.0));
        CHECK(slider->value() == 60.0); // on the configured step grid
    }

    SECTION("progress is a read-only percentage range") {
        auto progress_element =
            uia.find_by_control_type(element.Get(), UIA_ProgressBarControlTypeId);
        REQUIRE(progress_element != nullptr);
        ComPtr<IUnknown> unknown;
        require_ok(
            progress_element->GetCurrentPattern(UIA_RangeValuePatternId, unknown.GetAddressOf()));
        ComPtr<IUIAutomationRangeValuePattern> range;
        require_ok(unknown.As(&range));
        double value = -1.0;
        BOOL read_only = FALSE;
        require_ok(range->get_CurrentValue(&value));
        require_ok(range->get_CurrentIsReadOnly(&read_only));
        CHECK(value == 25.0);
        CHECK(read_only == TRUE);
        CHECK(FAILED(range->SetValue(90.0)));
    }

    SECTION("spin button accepts programmatic values") {
        auto spin_element = uia.find_by_control_type(element.Get(), UIA_SpinnerControlTypeId);
        REQUIRE(spin_element != nullptr);
        ComPtr<IUnknown> unknown;
        require_ok(
            spin_element->GetCurrentPattern(UIA_RangeValuePatternId, unknown.GetAddressOf()));
        ComPtr<IUIAutomationRangeValuePattern> range;
        require_ok(unknown.As(&range));
        require_ok(range->SetValue(4.0));
        CHECK(spin->value() == 4.0);
        // Out-of-range requests clamp to the range, not past it.
        require_ok(range->SetValue(99.0));
        CHECK(spin->value() == 10.0);
    }
}

TEST_CASE("Win32 UIA selection drives tabs", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA tab probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto tabs = nk::TabBar::create();
    tabs->append_tab("Alpha");
    tabs->append_tab("Bravo");
    tabs->append_tab("Charlie");
    tabs->set_selected_index(0);
    root->append(tabs);
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(nk::window_hwnd(window), &element));

    // The tab strip reports the selected tab.
    auto strip = uia.find_by_control_type(element.Get(), UIA_TabControlTypeId);
    REQUIRE(strip != nullptr);
    ComPtr<IUnknown> selection_unknown;
    require_ok(strip->GetCurrentPattern(UIA_SelectionPatternId, selection_unknown.GetAddressOf()));
    ComPtr<IUIAutomationSelectionPattern> selection;
    require_ok(selection_unknown.As(&selection));
    BOOL multiple = TRUE;
    require_ok(selection->get_CurrentCanSelectMultiple(&multiple));
    CHECK(multiple == FALSE);
    ComPtr<IUIAutomationElementArray> selected;
    require_ok(selection->GetCurrentSelection(selected.GetAddressOf()));
    int selected_count = -1;
    require_ok(selected->get_Length(&selected_count));
    REQUIRE(selected_count == 1);
    ComPtr<IUIAutomationElement> selected_tab;
    require_ok(selected->GetElement(0, selected_tab.GetAddressOf()));
    CHECK(narrow(uia.string_property(selected_tab.Get(), UIA_NamePropertyId)) == "Alpha");

    // Selecting a tab item through UIA moves the toolkit selection.
    auto bravo = uia.find_by_name(element.Get(), "Bravo");
    REQUIRE(bravo != nullptr);
    CHECK(uia.int_property(bravo.Get(), UIA_ControlTypePropertyId) == UIA_TabItemControlTypeId);
    ComPtr<IUnknown> item_unknown;
    require_ok(bravo->GetCurrentPattern(UIA_SelectionItemPatternId, item_unknown.GetAddressOf()));
    ComPtr<IUIAutomationSelectionItemPattern> item;
    require_ok(item_unknown.As(&item));
    BOOL is_selected = TRUE;
    require_ok(item->get_CurrentIsSelected(&is_selected));
    CHECK(is_selected == FALSE);
    require_ok(item->Select());
    CHECK(tabs->selected_index() == 1);
    require_ok(item->get_CurrentIsSelected(&is_selected));
    CHECK(is_selected == TRUE);
}

TEST_CASE("Win32 UIA combo boxes expand and collapse", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA combo probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto combo = nk::ComboBox::create();
    combo->set_items({"First", "Second", "Third"});
    root->append(combo);
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(nk::window_hwnd(window), &element));
    auto combo_element = uia.find_by_control_type(element.Get(), UIA_ComboBoxControlTypeId);
    REQUIRE(combo_element != nullptr);

    ComPtr<IUnknown> unknown;
    require_ok(
        combo_element->GetCurrentPattern(UIA_ExpandCollapsePatternId, unknown.GetAddressOf()));
    ComPtr<IUIAutomationExpandCollapsePattern> expand;
    require_ok(unknown.As(&expand));
    ExpandCollapseState state = ExpandCollapseState_Expanded;
    require_ok(expand->get_CurrentExpandCollapseState(&state));
    CHECK(state == ExpandCollapseState_Collapsed); // items hidden while closed

    require_ok(expand->Expand());
    require_ok(expand->get_CurrentExpandCollapseState(&state));
    CHECK(state == ExpandCollapseState_Expanded); // popup items are exposed

    require_ok(expand->Collapse());
    require_ok(expand->get_CurrentExpandCollapseState(&state));
    CHECK(state == ExpandCollapseState_Collapsed);
}

TEST_CASE("Win32 UIA marks secure editors", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA secure probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto plain = nk::TextField::create();
    plain->set_placeholder("Plain field");
    auto secret = nk::TextField::create();
    secret->set_placeholder("Password");
    secret->set_secure_text_entry(true);
    root->append(plain);
    root->append(secret);
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(nk::window_hwnd(window), &element));

    auto plain_element = uia.find_by_name(element.Get(), "Plain field");
    REQUIRE(plain_element != nullptr);
    CHECK_FALSE(uia.bool_property(plain_element.Get(), UIA_IsPasswordPropertyId));

    auto secret_element = uia.find_by_name(element.Get(), "Password");
    REQUIRE(secret_element != nullptr);
    CHECK(uia.bool_property(secret_element.Get(), UIA_IsPasswordPropertyId));
}

TEST_CASE("Win32 UIA SetFocus moves toolkit focus", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA focus probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto first = nk::Button::create("First target");
    auto second = nk::Button::create("Second target");
    root->append(first);
    root->append(second);
    window.set_child(root);
    window.present();

    const HWND hwnd = nk::window_hwnd(window);
    REQUIRE(hwnd != nullptr);

    nk::detail::AccessibilityTree tree(window);
    std::optional<nk::detail::AccessibleId> second_id;
    for (const auto id : tree.root_children()) {
        if (tree.info(id)->name == "Second target") {
            second_id = id;
        }
    }
    REQUIRE(second_id.has_value());

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(hwnd, &element));
    ComPtr<IUIAutomationElement> second_element = uia.find_by_name(element.Get(), "Second target");
    REQUIRE(second_element != nullptr);
    REQUIRE(SUCCEEDED(second_element->SetFocus()));

    const auto focused = tree.focused();
    REQUIRE(focused.has_value());
    CHECK(*focused == *second_id);
    CHECK(uia.bool_property(second_element.Get(), UIA_HasKeyboardFocusPropertyId));
}

TEST_CASE("Win32 UIA scrolls elements into view", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA scroll probe", .width = 240, .height = 300});
    auto scroll = nk::ScrollArea::create();
    auto rows = probe_box();
    for (int i = 0; i < 30; ++i) {
        rows->append(nk::Label::create("Row " + std::to_string(i)));
    }
    scroll->set_content(rows);
    window.set_child(scroll);
    window.present();
    REQUIRE(app.event_loop().poll());

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(nk::window_hwnd(window), &element));

    // A far-down row is outside the viewport; ScrollIntoView scrolls to it.
    auto row_element = uia.find_by_name(element.Get(), "Row 25");
    REQUIRE(row_element != nullptr);
    CHECK(scroll->v_offset() == 0.0F);
    ComPtr<IUnknown> unknown;
    require_ok(row_element->GetCurrentPattern(UIA_ScrollItemPatternId, unknown.GetAddressOf()));
    ComPtr<IUIAutomationScrollItemPattern> scroll_item;
    require_ok(unknown.As(&scroll_item));
    require_ok(scroll_item->ScrollIntoView());
    CHECK(scroll->v_offset() > 0.0F);
}

TEST_CASE("Win32 UIA exposes editor text through the Text pattern", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA text probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto field = nk::TextField::create();
    field->set_placeholder("Editor");
    field->set_text("Hello World");
    root->append(field);
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());
    field->grab_focus();

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(nk::window_hwnd(window), &element));
    auto field_element = uia.find_by_name(element.Get(), "Editor");
    REQUIRE(field_element != nullptr);

    ComPtr<IUnknown> unknown;
    require_ok(field_element->GetCurrentPattern(UIA_TextPatternId, unknown.GetAddressOf()));
    ComPtr<IUIAutomationTextPattern> text;
    require_ok(unknown.As(&text));

    SupportedTextSelection selection_mode = SupportedTextSelection_None;
    require_ok(text->get_SupportedTextSelection(&selection_mode));
    CHECK(selection_mode == SupportedTextSelection_Single);

    // The document range covers the whole committed text.
    ComPtr<IUIAutomationTextRange> document_range;
    require_ok(text->get_DocumentRange(document_range.GetAddressOf()));
    BSTR document_text = nullptr;
    require_ok(document_range->GetText(-1, &document_text));
    CHECK(narrow(document_text != nullptr ? document_text : L"") == "Hello World");
    SysFreeString(document_text);

    // The editor selection (cursor..anchor) reads through GetSelection.
    field->select_all();
    ComPtr<IUIAutomationTextRangeArray> selection;
    require_ok(text->GetSelection(selection.GetAddressOf()));
    int selection_count = 0;
    require_ok(selection->get_Length(&selection_count));
    REQUIRE(selection_count == 1);
    ComPtr<IUIAutomationTextRange> selected;
    require_ok(selection->GetElement(0, selected.GetAddressOf()));
    BSTR selected_text = nullptr;
    require_ok(selected->GetText(-1, &selected_text));
    CHECK(narrow(selected_text != nullptr ? selected_text : L"") == "Hello World");
    SysFreeString(selected_text);

    // Word movement: move one word from the document start, then expand.
    ComPtr<IUIAutomationTextRange> word_range;
    require_ok(document_range->Clone(word_range.GetAddressOf()));
    int moved = 0;
    require_ok(word_range->Move(TextUnit_Word, 1, &moved));
    CHECK(moved == 1);
    BSTR moved_text = nullptr;
    require_ok(word_range->GetText(-1, &moved_text));
    CHECK(narrow(moved_text != nullptr ? moved_text : L"").empty()); // collapsed
    SysFreeString(moved_text);
    require_ok(word_range->ExpandToEnclosingUnit(TextUnit_Word));
    BSTR expanded_text = nullptr;
    require_ok(word_range->GetText(-1, &expanded_text));
    CHECK(narrow(expanded_text != nullptr ? expanded_text : L"") == "World");
    SysFreeString(expanded_text);
}

TEST_CASE("Win32 UIA resolves labelled-by relations", "[windows][uia]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "UIA relation probe", .width = 360, .height = 240});
    auto root = probe_box();
    auto label = nk::Label::create("Search query");
    label->set_debug_name("search-query-label");
    auto field = nk::TextField::create();
    field->set_placeholder("Type here");
    root->append(label);
    root->append(field);
    field->ensure_accessible().set_relation(nk::AccessibleRelationKind::LabelledBy,
                                            std::string(label->debug_name()));
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    UiaClient uia;
    ComPtr<IUIAutomationElement> element;
    require_ok(uia.get()->ElementFromHandle(nk::window_hwnd(window), &element));
    auto field_element = uia.find_by_name(element.Get(), "Type here");
    REQUIRE(field_element != nullptr);

    VARIANT labeled_by{};
    require_ok(field_element->GetCurrentPropertyValue(UIA_LabeledByPropertyId, &labeled_by));
    REQUIRE(labeled_by.vt == VT_UNKNOWN);
    REQUIRE(labeled_by.punkVal != nullptr);
    // The resolved target is the label widget's element. VariantClear owns
    // the single release of the client-side wrapper.
    const auto label_element = static_cast<IUIAutomationElement*>(labeled_by.punkVal);
    CHECK(narrow(uia.string_property(label_element, UIA_NamePropertyId)) == "Search query");
    VariantClear(&labeled_by);
}

#endif // defined(_WIN32)
