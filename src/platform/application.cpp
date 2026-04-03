#include <cassert>
#include <nk/foundation/logging.h>
#include <nk/platform/application.h>
#include <nk/platform/platform_backend.h>
#include <nk/style/theme.h>
#include <nk/style/theme_selection.h>

namespace nk {

namespace {
Application* g_instance = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

struct Application::Impl {
    ApplicationConfig config;
    EventLoop event_loop;
    std::unique_ptr<PlatformBackend> backend;
    SystemPreferences system_preferences;
    ThemeSelection theme_selection;
    ResolvedThemeSelection resolved_theme_selection;
    std::string clipboard_text;
    std::vector<NativeMenu> native_app_menus;
    Signal<const SystemPreferences&> system_preferences_changed;
    Signal<const ThemeSelection&> theme_selection_changed;
    Signal<std::string_view> native_app_menu_action;
    CallbackHandle system_preferences_observer;
    std::chrono::milliseconds system_preferences_poll_interval{std::chrono::seconds(1)};
    bool system_preferences_observation_enabled = true;
    bool using_native_system_preferences_observer = false;
};

namespace {

template <typename ImplT> void initialize_platform_backend(ImplT& impl) {
    impl.backend = PlatformBackend::create();
    if (!impl.backend) {
        return;
    }

    auto result = impl.backend->initialize();
    if (result) {
        return;
    }

    NK_LOG_ERROR("App", result.error().c_str());
    impl.backend.reset();
}

template <typename ImplT> void apply_visual_policy(ImplT& impl) {
    impl.resolved_theme_selection =
        resolve_theme_selection(impl.theme_selection, impl.system_preferences);
    Theme::set_active(make_theme(impl.resolved_theme_selection, impl.system_preferences));
}

template <typename ImplT> void initialize_visual_policy(ImplT& impl) {
    if (impl.backend) {
        impl.system_preferences = impl.backend->system_preferences();
    }
    apply_visual_policy(impl);
}

template <typename ImplT>
void handle_system_preferences_update(ImplT& impl, SystemPreferences latest) {
    if (latest == impl.system_preferences) {
        return;
    }

    impl.system_preferences = std::move(latest);
    apply_visual_policy(impl);
    impl.system_preferences_changed.emit(impl.system_preferences);
}

template <typename ImplT> void stop_system_preferences_observer(ImplT& impl) {
    if (impl.system_preferences_observer.valid()) {
        impl.event_loop.cancel(impl.system_preferences_observer);
        impl.system_preferences_observer = {};
    }
    if (impl.using_native_system_preferences_observer && impl.backend) {
        impl.backend->stop_system_preferences_observation();
        impl.using_native_system_preferences_observer = false;
    }
}

template <typename ImplT> void start_system_preferences_observer(ImplT& impl) {
    stop_system_preferences_observer(impl);
    if (!impl.system_preferences_observation_enabled || !impl.backend) {
        return;
    }

    if (impl.backend->supports_system_preferences_observation()) {
        impl.backend->start_system_preferences_observation(
            [&impl](const SystemPreferences& latest) {
                impl.event_loop.post(
                    [&impl, latest] { handle_system_preferences_update(impl, latest); });
            });
        impl.using_native_system_preferences_observer = true;
        return;
    }

    impl.system_preferences_observer =
        impl.event_loop.set_interval(impl.system_preferences_poll_interval, [&impl] {
            handle_system_preferences_update(impl, impl.backend->system_preferences());
        });
}

} // namespace

Application::Application(int /*argc*/, char** /*argv*/) : impl_(std::make_unique<Impl>()) {
    assert(g_instance == nullptr && "Only one Application may exist at a time");
    g_instance = this;
    initialize_platform_backend(*impl_);
    initialize_visual_policy(*impl_);
    start_system_preferences_observer(*impl_);
    NK_LOG_DEBUG("App", "Application created (from argv)");
}

Application::Application(ApplicationConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    assert(g_instance == nullptr && "Only one Application may exist at a time");
    g_instance = this;
    initialize_platform_backend(*impl_);
    initialize_visual_policy(*impl_);
    start_system_preferences_observer(*impl_);
    NK_LOG_DEBUG("App", "Application created");
}

Application::~Application() {
    stop_system_preferences_observer(*impl_);
    if (impl_->backend) {
        impl_->backend->shutdown();
    }
    g_instance = nullptr;
    NK_LOG_DEBUG("App", "Application destroyed");
}

int Application::run() {
    NK_LOG_INFO("App", "Entering event loop");
    if (impl_->backend) {
        return impl_->backend->run_event_loop(impl_->event_loop);
    }
    // Fallback: basic polling loop (no platform backend).
    return impl_->event_loop.run();
}

void Application::quit(int exit_code) {
    impl_->event_loop.quit(exit_code);
    if (impl_->backend) {
        impl_->backend->request_quit(exit_code);
    }
}

EventLoop& Application::event_loop() {
    return impl_->event_loop;
}

std::string_view Application::app_id() const {
    return impl_->config.app_id;
}

std::string_view Application::app_name() const {
    return impl_->config.app_name;
}

const SystemPreferences& Application::system_preferences() const {
    return impl_->system_preferences;
}

void Application::refresh_system_preferences() {
    if (impl_->backend) {
        handle_system_preferences_update(*impl_, impl_->backend->system_preferences());
    } else {
        handle_system_preferences_update(*impl_, {});
    }
}

const ThemeSelection& Application::theme_selection() const {
    return impl_->theme_selection;
}

void Application::set_theme_selection(ThemeSelection selection) {
    if (impl_->theme_selection == selection) {
        return;
    }
    impl_->theme_selection = std::move(selection);
    apply_visual_policy(*impl_);
    impl_->theme_selection_changed.emit(impl_->theme_selection);
}

void Application::set_system_preferences_observation_enabled(bool enabled) {
    if (impl_->system_preferences_observation_enabled == enabled) {
        return;
    }
    impl_->system_preferences_observation_enabled = enabled;
    if (enabled) {
        start_system_preferences_observer(*impl_);
    } else {
        stop_system_preferences_observer(*impl_);
    }
}

bool Application::is_system_preferences_observation_enabled() const {
    return impl_->system_preferences_observation_enabled;
}

void Application::set_system_preferences_observation_interval(std::chrono::milliseconds interval) {
    if (interval <= std::chrono::milliseconds{0}) {
        interval = std::chrono::milliseconds{250};
    }
    if (impl_->system_preferences_poll_interval == interval) {
        return;
    }
    impl_->system_preferences_poll_interval = interval;
    if (impl_->system_preferences_observation_enabled) {
        start_system_preferences_observer(*impl_);
    }
}

std::chrono::milliseconds Application::system_preferences_observation_interval() const {
    return impl_->system_preferences_poll_interval;
}

Application* Application::instance() {
    return g_instance;
}

bool Application::has_platform_backend() const {
    return impl_->backend != nullptr;
}

PlatformBackend& Application::platform_backend() {
    assert(impl_->backend && "No platform backend available");
    return *impl_->backend;
}

bool Application::supports_open_file_dialog() const {
    return impl_->backend != nullptr && impl_->backend->supports_open_file_dialog();
}

OpenFileDialogResult Application::open_file_dialog(std::string_view title,
                                                   const std::vector<std::string>& filters) {
    if (!impl_->backend) {
        return Unexpected(FileDialogError::Unavailable);
    }
    if (!impl_->backend->supports_open_file_dialog()) {
        return Unexpected(FileDialogError::Unsupported);
    }
    return impl_->backend->show_open_file_dialog(title, filters);
}

bool Application::supports_clipboard_text() const {
    return impl_->backend != nullptr && impl_->backend->supports_clipboard_text();
}

std::string Application::clipboard_text() const {
    if (impl_->backend != nullptr && impl_->backend->supports_clipboard_text()) {
        return impl_->backend->clipboard_text();
    }
    return impl_->clipboard_text;
}

void Application::set_clipboard_text(std::string text) {
    impl_->clipboard_text = std::move(text);
    if (impl_->backend != nullptr && impl_->backend->supports_clipboard_text()) {
        impl_->backend->set_clipboard_text(impl_->clipboard_text);
    }
}

bool Application::supports_native_app_menu() const {
    return impl_->backend != nullptr && impl_->backend->supports_native_app_menu();
}

void Application::set_native_app_menu(std::vector<NativeMenu> menus) {
    impl_->native_app_menus = std::move(menus);
    if (impl_->backend == nullptr || !impl_->backend->supports_native_app_menu()) {
        return;
    }

    impl_->backend->set_native_app_menu(
        impl_->native_app_menus,
        [this](std::string_view action_name) { impl_->native_app_menu_action.emit(action_name); });
}

Signal<const SystemPreferences&>& Application::on_system_preferences_changed() {
    return impl_->system_preferences_changed;
}

Signal<const ThemeSelection&>& Application::on_theme_selection_changed() {
    return impl_->theme_selection_changed;
}

Signal<std::string_view>& Application::on_native_app_menu_action() {
    return impl_->native_app_menu_action;
}

} // namespace nk
