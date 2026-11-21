#include "uia_provider.h"

#include "../../accessibility/accessibility_tree.h"
#include "win32_backend.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nk/platform/window.h>
#include <nk/ui_core/state_flags.h>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <wrl/client.h>

// objbase.h defines the `interface` keyword the UI Automation headers use;
// WIN32_LEAN_AND_MEAN strips it out of windows.h.
#include <objbase.h>
#include <uiautomation.h>

namespace nk::detail {

namespace {

using Microsoft::WRL::ComPtr;

/// VARIANT with RAII clearing, so GetPropertyValue never leaks on early return.
class UniqueVariant {
public:
    UniqueVariant() { VariantInit(&value); }

    ~UniqueVariant() { VariantClear(&value); }

    UniqueVariant(const UniqueVariant&) = delete;
    UniqueVariant& operator=(const UniqueVariant&) = delete;

    [[nodiscard]] VARIANT* get() { return &value; }

    /// Transfer ownership of the payload into `out` without copying.
    void release_into(VARIANT* out) {
        *out = value;
        value.vt = VT_EMPTY;
    }

private:
    VARIANT value{};
};

void set_bstr(VARIANT* variant, const std::wstring& text) {
    variant->vt = VT_BSTR;
    variant->bstrVal = SysAllocStringLen(text.c_str(), static_cast<UINT>(text.size()));
}

void set_bool(VARIANT* variant, BOOL value) {
    variant->vt = VT_BOOL;
    variant->boolVal = value != FALSE ? VARIANT_TRUE : VARIANT_FALSE;
}

void set_i4(VARIANT* variant, int value) {
    variant->vt = VT_I4;
    variant->lVal = value;
}

/// SAFEARRAY with four doubles (left, top, width, height).
SAFEARRAY* rect_safearray(const UiaRect& rect) {
    SAFEARRAYBOUND bound{};
    bound.lLbound = 0;
    bound.cElements = 4;
    SAFEARRAY* array = SafeArrayCreate(VT_R8, 1, &bound);
    if (array == nullptr) {
        return nullptr;
    }
    const double values[4] = {rect.left, rect.top, rect.width, rect.height};
    for (LONG index = 0; index < 4; ++index) {
        double element = values[index];
        if (FAILED(SafeArrayPutElement(array, &index, &element))) {
            SafeArrayDestroy(array);
            return nullptr;
        }
    }
    return array;
}

/// Numeric prefix of an accessible value string ("42", "37%"), if any.
[[nodiscard]] std::optional<double> parse_accessible_number(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto [pointer, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || pointer == text.data()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool has_action(const AccessibleNodeInfo& info, AccessibleAction action) {
    return std::ranges::find(info.actions, action) != info.actions.end();
}

[[nodiscard]] bool is_toggle_role(AccessibleRole role) {
    return role == AccessibleRole::CheckBox || role == AccessibleRole::RadioButton ||
           role == AccessibleRole::ToggleButton;
}

/// Percent-bearing roles whose string value maps onto a 0..100 read-only
/// range, pending numeric min/max support in the accessibility model.
[[nodiscard]] bool is_percent_range_role(AccessibleRole role) {
    return role == AccessibleRole::ProgressBar || role == AccessibleRole::Meter;
}

class UiaFragment;

/// Liveness token shared by the bridge, the fragments, and the pattern
/// adapters: clients can hold pattern interfaces past window teardown, so
/// every object that dereferences the site checks the token first and answers
/// UIA_E_ELEMENTNOTAVAILABLE once the bridge disconnected.
using UiaLiveness = std::shared_ptr<std::atomic<bool>>;

/// Shared hub every provider reads: the live accessibility tree for one
/// window, its HWND, the lazily created element providers, and the fragment
/// root. Owned by Win32UiaBridge::Impl; providers hold a raw pointer plus the
/// liveness token and are all disconnected before the hub is destroyed.
struct UiaSite {
    HWND hwnd = nullptr;
    Window* window = nullptr;
    std::unique_ptr<AccessibilityTree> tree;
    ComPtr<IRawElementProviderFragmentRoot> root;
    std::unordered_map<AccessibleId, void*> providers;
    UiaLiveness alive = std::make_shared<std::atomic<bool>>(true);
    bool clients_connected = false;

    /// Provider for an exposed element, created on first use. The site keeps
    /// the owning reference; UIA adds its own while clients hold the element.
    [[nodiscard]] UiaFragment* fragment_for(AccessibleId id);

    /// Convert a window-logical rect into physical screen coordinates.
    [[nodiscard]] UiaRect screen_rect(Rect logical) const {
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        const float scale = win32_dpi_scale_for_hwnd(hwnd);
        return {
            static_cast<double>(origin.x) + static_cast<double>(logical.x * scale),
            static_cast<double>(origin.y) + static_cast<double>(logical.y * scale),
            static_cast<double>(logical.width * scale),
            static_cast<double>(logical.height * scale),
        };
    }

    /// Convert a physical screen point into window-logical coordinates.
    [[nodiscard]] Point logical_point(double x, double y) const {
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        const float scale = win32_dpi_scale_for_hwnd(hwnd);
        const float inverse = scale > 0.0F ? 1.0F / scale : 1.0F;
        return {
            static_cast<float>(x - origin.x) * inverse,
            static_cast<float>(y - origin.y) * inverse,
        };
    }
};

// --- Pattern adapters --------------------------------------------------------
// IValueProvider and IRangeValueProvider both declare get_IsReadOnly(BOOL*),
// so they cannot be implemented on one C++ class; every pattern is a separate
// stateless adapter object. Each hand-rolls IUnknown like the rest of the
// backend's COM objects (no shared base: that would diamond through IUnknown).

/// Everything a pattern adapter needs: the site, the element id, and the
/// liveness token guarding against post-teardown calls.
struct UiaPatternState {
    UiaSite* site = nullptr;
    AccessibleId id = 0;
    UiaLiveness alive;

    [[nodiscard]] bool live() const { return alive->load(); }
};

class UiaInvokePattern final : public IInvokeProvider {
public:
    UiaInvokePattern(UiaSite* site, AccessibleId id, const UiaLiveness& alive)
        : state_{site, id, alive} {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IInvokeProvider) {
            *out = static_cast<IInvokeProvider*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE Invoke() override {
        if (!state_.live() || !state_.site->tree->is_exposed(state_.id)) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const bool performed = state_.site->tree->perform(state_.id, AccessibleAction::Activate) ||
                               state_.site->tree->perform(state_.id, AccessibleAction::Toggle);
        return performed ? S_OK : UIA_E_INVALIDOPERATION;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    UiaPatternState state_;
};

class UiaTogglePattern final : public IToggleProvider {
public:
    UiaTogglePattern(UiaSite* site, AccessibleId id, const UiaLiveness& alive)
        : state_{site, id, alive} {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IToggleProvider) {
            *out = static_cast<IToggleProvider*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const auto info = state_.site->tree->info(state_.id);
        if (!state_.live() || !info.has_value()) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const bool checked = (info->state & StateFlags::Checked) != StateFlags::None;
        *out = checked ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override {
        if (!state_.live() || !state_.site->tree->is_exposed(state_.id)) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const bool performed = state_.site->tree->perform(state_.id, AccessibleAction::Toggle) ||
                               state_.site->tree->perform(state_.id, AccessibleAction::Activate);
        return performed ? S_OK : UIA_E_INVALIDOPERATION;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    UiaPatternState state_;
};

class UiaValuePattern final : public IValueProvider {
public:
    UiaValuePattern(UiaSite* site, AccessibleId id, const UiaLiveness& alive)
        : state_{site, id, alive} {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IValueProvider) {
            *out = static_cast<IValueProvider*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const auto info = state_.site->tree->info(state_.id);
        if (!state_.live() || !info.has_value()) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const auto wide = win32_wide_from_utf8(info->value);
        *out = SysAllocStringLen(wide.c_str(), static_cast<UINT>(wide.size()));
        return *out != nullptr ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        // Read-only: the model exposes values, not value setters.
        *out = TRUE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR /*value*/) override {
        return state_.live() ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    UiaPatternState state_;
};

class UiaRangeValuePattern final : public IRangeValueProvider {
public:
    UiaRangeValuePattern(UiaSite* site, AccessibleId id, const UiaLiveness& alive)
        : state_{site, id, alive} {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IRangeValueProvider) {
            *out = static_cast<IRangeValueProvider*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE get_Value(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const auto info = state_.site->tree->info(state_.id);
        if (!state_.live() || !info.has_value()) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = std::clamp(parse_accessible_number(info->value).value_or(0.0), 0.0, 100.0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = TRUE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 100.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 0.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 0.0; // read-only range: no changes
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 0.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double /*value*/) override {
        return state_.live() ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    UiaPatternState state_;
};

// --- Element provider --------------------------------------------------------

/// UIA provider for one exposed element. Answers every property live from the
/// accessibility tree; pattern calls run tree actions directly (providers are
/// handed out on the UI-thread STA, so UIA calls back on the UI thread).
class UiaFragment final : public IRawElementProviderSimple, public IRawElementProviderFragment {
public:
    UiaFragment(UiaSite* site, AccessibleId id) : site_(site), id_(id), alive_(site->alive) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == IID_IRawElementProviderFragment) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        // Server-side provider with COM-threading support: providers are handed
        // out on the UI-thread STA, so every UIA callback arrives on the UI
        // thread and can touch the widget tree without locks or marshaling.
        *out = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        const auto info = site_->tree->info(id_);
        if (!info.has_value()) {
            return S_OK; // destroyed widgets simply expose no patterns
        }
        switch (pattern) {
        case UIA_InvokePatternId:
            if (has_action(*info, AccessibleAction::Activate)) {
                *out = new UiaInvokePattern(site_, id_, alive_);
            }
            break;
        case UIA_TogglePatternId:
            if (has_action(*info, AccessibleAction::Toggle) || is_toggle_role(info->role)) {
                *out = new UiaTogglePattern(site_, id_, alive_);
            }
            break;
        case UIA_ValuePatternId:
            if (!info->value.empty() || info->role == AccessibleRole::TextInput) {
                *out = new UiaValuePattern(site_, id_, alive_);
            }
            break;
        case UIA_RangeValuePatternId:
            if (is_percent_range_role(info->role) && parse_accessible_number(info->value)) {
                *out = new UiaRangeValuePattern(site_, id_, alive_);
            }
            break;
        default:
            break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        UniqueVariant variant;
        const auto info = site_->tree->info(id_);
        if (!info.has_value()) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        switch (property) {
        case UIA_NamePropertyId:
            set_bstr(variant.get(), win32_wide_from_utf8(info->name));
            break;
        case UIA_ControlTypePropertyId:
            set_i4(variant.get(), win32_uia_control_type(info->role, info->type_name));
            break;
        case UIA_IsControlElementPropertyId:
            set_bool(variant.get(), TRUE);
            break;
        case UIA_IsContentElementPropertyId:
            // Separators decorate rather than carry content.
            set_bool(variant.get(), info->role == AccessibleRole::Separator ? FALSE : TRUE);
            break;
        case UIA_IsEnabledPropertyId:
            set_bool(variant.get(), info->enabled ? TRUE : FALSE);
            break;
        case UIA_IsKeyboardFocusablePropertyId:
            set_bool(variant.get(), info->focusable ? TRUE : FALSE);
            break;
        case UIA_HasKeyboardFocusPropertyId:
            set_bool(variant.get(), info->focused ? TRUE : FALSE);
            break;
        case UIA_BoundingRectanglePropertyId: {
            const auto rect = rect_safearray(site_->screen_rect(info->bounds));
            if (rect != nullptr) {
                variant.get()->vt = VT_R8 | VT_ARRAY;
                variant.get()->parray = rect;
            }
            break;
        }
        case UIA_AutomationIdPropertyId:
            set_bstr(variant.get(), win32_wide_from_utf8(info->debug_name));
            break;
        case UIA_HelpTextPropertyId:
            set_bstr(variant.get(), win32_wide_from_utf8(info->description));
            break;
        case UIA_FrameworkIdPropertyId:
            set_bstr(variant.get(), L"NodalKit");
            break;
        case UIA_ProviderDescriptionPropertyId:
            set_bstr(variant.get(), L"NodalKit:Win32:UIA");
            break;
        case UIA_NativeWindowHandlePropertyId:
            // Fragments are windowless; only the root has a real HWND.
            set_i4(variant.get(), 0);
            break;
        case UIA_ProcessIdPropertyId:
            set_i4(variant.get(), static_cast<int>(GetCurrentProcessId()));
            break;
        case UIA_IsPasswordPropertyId:
            // Not exposed until the accessibility model carries editor
            // security state.
            set_bool(variant.get(), FALSE);
            break;
        default:
            break; // VT_EMPTY: UIA falls back to its defaults for this element
        }
        variant.release_into(out);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        // Only window-hosted (root) elements have a default host provider.
        *out = nullptr;
        return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        if (!site_->tree->is_exposed(id_)) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }

        std::optional<AccessibleId> target;
        switch (direction) {
        case NavigateDirection_Parent: {
            // A nullopt parent means the window root is the parent.
            const auto parent = site_->tree->parent(id_);
            if (parent.has_value()) {
                *out = site_->fragment_for(*parent);
                (*out)->AddRef();
            } else {
                site_->root.CopyTo(out);
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling: {
            const auto siblings = sibling_ids();
            const auto found = std::ranges::find(siblings, id_);
            if (found == siblings.end()) {
                return S_OK;
            }
            if (direction == NavigateDirection_NextSibling && std::next(found) != siblings.end()) {
                target = *std::next(found);
            } else if (direction == NavigateDirection_PreviousSibling &&
                       found != siblings.begin()) {
                target = *std::prev(found);
            }
            break;
        }
        case NavigateDirection_FirstChild: {
            const auto children = site_->tree->children(id_);
            if (!children.empty()) {
                target = children.front();
            }
            break;
        }
        case NavigateDirection_LastChild: {
            const auto children = site_->tree->children(id_);
            if (!children.empty()) {
                target = children.back();
            }
            break;
        }
        default:
            return S_OK;
        }
        if (target.has_value()) {
            *out = site_->fragment_for(*target);
            (*out)->AddRef();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        // Tree ids are dense per window and never reused, which is exactly the
        // runtime-id suffix contract.
        constexpr LONG kRuntimeIdLength = 2;
        const LONG elements[kRuntimeIdLength] = {UiaAppendRuntimeId, static_cast<LONG>(id_)};
        SAFEARRAYBOUND bound{};
        bound.lLbound = 0;
        bound.cElements = kRuntimeIdLength;
        *out = SafeArrayCreate(VT_I4, 1, &bound);
        if (*out == nullptr) {
            return E_OUTOFMEMORY;
        }
        for (LONG index = 0; index < kRuntimeIdLength; ++index) {
            const LONG element = elements[index];
            if (FAILED(SafeArrayPutElement(*out, &index, const_cast<LONG*>(&element)))) {
                SafeArrayDestroy(*out);
                *out = nullptr;
                return E_FAIL;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const auto info = site_->tree->info(id_);
        if (!info.has_value()) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = site_->screen_rect(info->bounds);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        // The toolkit hosts no other automation frameworks.
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        if (!site_->tree->is_exposed(id_)) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        (void)site_->tree->focus(id_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (site_->root == nullptr) {
            *out = nullptr;
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        site_->root.CopyTo(out);
        return S_OK;
    }

private:
    /// Ids of this element's siblings (parent's children, or the root children
    /// for top-level elements), used to answer sibling navigation.
    [[nodiscard]] std::vector<AccessibleId> sibling_ids() const {
        const auto parent = site_->tree->parent(id_);
        return parent.has_value() ? site_->tree->children(*parent) : site_->tree->root_children();
    }

    std::atomic<ULONG> ref_count_{1};
    UiaSite* site_;
    AccessibleId id_;
    UiaLiveness alive_;
};

// --- Fragment root provider --------------------------------------------------

/// UIA provider for the window itself: the fragment root handed to
/// UiaReturnRawElementProvider. Window-level defaults (name, runtime id,
/// process id, class name, base geometry) are left to the host window provider
/// by returning VT_EMPTY; only the fragment tree is described here.
class UiaRootFragment final : public IRawElementProviderSimple,
                              public IRawElementProviderFragment,
                              public IRawElementProviderFragmentRoot {
public:
    explicit UiaRootFragment(UiaSite* site) : site_(site), alive_(site->alive) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == IID_IRawElementProviderFragment) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else if (riid == IID_IRawElementProviderFragmentRoot) {
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID /*pattern*/, IUnknown** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        // Window and selection patterns are not implemented yet.
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        UniqueVariant variant;
        switch (property) {
        case UIA_ControlTypePropertyId:
            set_i4(variant.get(), UIA_WindowControlTypeId);
            break;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            set_bool(variant.get(), TRUE);
            break;
        case UIA_FrameworkIdPropertyId:
            set_bstr(variant.get(), L"NodalKit");
            break;
        case UIA_ProviderDescriptionPropertyId:
            set_bstr(variant.get(), L"NodalKit:Win32:UIA");
            break;
        default:
            break; // VT_EMPTY: the host window provider supplies the rest
        }
        variant.release_into(out);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        return UiaHostProviderFromHwnd(site_->hwnd, out);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
            const auto children = site_->tree->root_children();
            if (children.empty()) {
                return S_OK;
            }
            *out = site_->fragment_for(direction == NavigateDirection_FirstChild ? children.front()
                                                                                 : children.back());
            (*out)->AddRef();
        }
        // Parent/sibling navigation above the root is the host's job.
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        // The host window provider derives the root runtime id from the HWND.
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        RECT client{};
        GetClientRect(site_->hwnd, &client);
        *out = site_->screen_rect({0.0F,
                                   0.0F,
                                   static_cast<float>(client.right - client.left),
                                   static_cast<float>(client.bottom - client.top)});
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        // UIA focuses the window first; nothing widget-side to do at the root.
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = this;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x,
                                                       double y,
                                                       IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        const Point logical = site_->logical_point(x, y);
        const auto hit = site_->tree->hit_test(logical);
        if (hit.has_value()) {
            *out = site_->fragment_for(*hit);
            (*out)->AddRef();
            return S_OK;
        }
        // No child at the point: the root itself receives the input.
        *out = this;
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        const auto focused = site_->tree->focused();
        if (focused.has_value()) {
            *out = site_->fragment_for(*focused);
            (*out)->AddRef();
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    UiaSite* site_;
    UiaLiveness alive_;
};

UiaFragment* UiaSite::fragment_for(AccessibleId id) {
    if (const auto found = providers.find(id); found != providers.end()) {
        return static_cast<UiaFragment*>(found->second);
    }
    auto* provider = new UiaFragment(this, id);
    providers.emplace(id, provider);
    return provider;
}

} // namespace

int win32_uia_control_type(AccessibleRole role, std::string_view type_name) {
    // Type-name special cases mirror the macOS bridge's mapping.
    if (type_name == "ComboBox") {
        return UIA_ComboBoxControlTypeId;
    }
    if (type_name == "TextArea") {
        // Multi-line edit; Document replaces Edit once text-range support
        // lands.
        return UIA_EditControlTypeId;
    }
    if (type_name == "Switch") {
        // UIA has no switch control type; a switch is an on/off checkbox.
        return UIA_CheckBoxControlTypeId;
    }
    switch (role) {
    case AccessibleRole::Button:
        return UIA_ButtonControlTypeId;
    case AccessibleRole::CheckBox:
    case AccessibleRole::ToggleButton:
        return UIA_CheckBoxControlTypeId;
    case AccessibleRole::RadioButton:
        return UIA_RadioButtonControlTypeId;
    case AccessibleRole::Link:
        return UIA_HyperlinkControlTypeId;
    case AccessibleRole::Label:
    case AccessibleRole::Status:
        return UIA_TextControlTypeId;
    case AccessibleRole::TextInput:
        return UIA_EditControlTypeId;
    case AccessibleRole::Slider:
        return UIA_SliderControlTypeId;
    case AccessibleRole::SpinButton:
        return UIA_SpinnerControlTypeId;
    case AccessibleRole::ProgressBar:
    case AccessibleRole::Meter:
        return UIA_ProgressBarControlTypeId;
    case AccessibleRole::List:
        return UIA_ListControlTypeId;
    case AccessibleRole::ListItem:
        return UIA_ListItemControlTypeId;
    case AccessibleRole::Menu:
        return UIA_MenuControlTypeId;
    case AccessibleRole::MenuBar:
        return UIA_MenuBarControlTypeId;
    case AccessibleRole::MenuItem:
        return UIA_MenuItemControlTypeId;
    case AccessibleRole::Tab:
        return UIA_TabItemControlTypeId;
    case AccessibleRole::TabList:
        return UIA_TabControlTypeId;
    case AccessibleRole::Tree:
        return UIA_TreeControlTypeId;
    case AccessibleRole::TreeItem:
        return UIA_TreeItemControlTypeId;
    case AccessibleRole::Grid:
        return UIA_DataGridControlTypeId;
    case AccessibleRole::GridCell:
        return UIA_DataItemControlTypeId;
    case AccessibleRole::Image:
        return UIA_ImageControlTypeId;
    case AccessibleRole::Toolbar:
        return UIA_ToolBarControlTypeId;
    case AccessibleRole::Separator:
        return UIA_SeparatorControlTypeId;
    case AccessibleRole::ScrollBar:
        return UIA_ScrollBarControlTypeId;
    case AccessibleRole::Dialog:
    case AccessibleRole::Group:
    case AccessibleRole::Navigation:
    case AccessibleRole::TabPanel:
        return UIA_GroupControlTypeId;
    case AccessibleRole::Window:
        return UIA_WindowControlTypeId;
    default:
        return UIA_CustomControlTypeId;
    }
}

struct Win32UiaBridge::Impl {
    UiaSite site;
    // Owning reference to the concrete root; site.root holds the handed-out
    // interface reference. Both are released in disconnect().
    UiaRootFragment* root = nullptr;
    std::optional<AccessibleId> last_focused;
};

Win32UiaBridge::Win32UiaBridge(HWND hwnd, Window& owner) : impl_(std::make_unique<Impl>()) {
    impl_->site.hwnd = hwnd;
    impl_->site.window = &owner;
    impl_->site.tree = std::make_unique<AccessibilityTree>(owner);
    impl_->root = new UiaRootFragment(&impl_->site);
    impl_->site.root = impl_->root;
}

Win32UiaBridge::~Win32UiaBridge() {
    disconnect();
}

LRESULT Win32UiaBridge::handle_get_object(WPARAM wparam, LPARAM lparam) {
    if (!is_uia_request(lparam)) {
        return 0;
    }
    impl_->site.clients_connected = true;
    return UiaReturnRawElementProvider(impl_->site.hwnd, wparam, lparam, impl_->root);
}

void Win32UiaBridge::disconnect() {
    auto& site = impl_->site;
    if (site.root == nullptr) {
        return; // already disconnected
    }
    // Invalidate every object clients may still hold, then drop the site's
    // owning references so each provider is destroyed exactly once its last
    // reference is gone.
    site.alive->store(false);
    for (auto& [id, provider] : site.providers) {
        (void)id;
        auto* fragment = static_cast<UiaFragment*>(provider);
        UiaDisconnectProvider(fragment);
        fragment->Release();
    }
    site.providers.clear();
    UiaDisconnectProvider(impl_->root);
    site.root.Reset();
    impl_->root->Release();
    impl_->root = nullptr;
    // The tree itself outlives the providers by construction (providers only
    // hold the site pointer), but resetting keeps teardown explicit.
    site.tree.reset();
    site.clients_connected = false;
}

void Win32UiaBridge::synchronize() {
    auto& site = impl_->site;
    if (!site.clients_connected) {
        return; // no client ever asked for the tree; nothing to maintain
    }
    // Retire providers of destroyed widgets regardless of listeners, so their
    // references do not outlive the widgets they describe.
    for (const AccessibleId id : site.tree->purge()) {
        if (const auto found = site.providers.find(id); found != site.providers.end()) {
            auto* provider = static_cast<UiaFragment*>(found->second);
            site.providers.erase(found);
            UiaDisconnectProvider(provider);
            provider->Release();
        }
    }
    if (UiaClientsAreListening() == FALSE) {
        return;
    }
    const auto focused = site.tree->focused();
    if (focused == impl_->last_focused) {
        return;
    }
    impl_->last_focused = focused;
    if (focused.has_value()) {
        UiaRaiseAutomationEvent(site.fragment_for(*focused), UIA_AutomationFocusChangedEventId);
    }
}

} // namespace nk::detail
