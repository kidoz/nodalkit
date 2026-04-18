// Auto-generated public API link audit for NodalKit.
//
// Goal: reference every public function, method, and free function declared in
// the public headers under include/nk/**. If this test links, every declared
// public symbol has a definition in libNodalKit. If it does NOT link, each
// linker error names a Shortcut-class finding (API declared but unimplemented).
//
// This test is intentionally headless — it only takes addresses and never runs
// the referencing function, so it requires no Application, Window, or display.
//
// Generated from the agent enumeration over include/nk/**/*.h. Do not edit by
// hand; regenerate by re-running the public API enumeration pass.

#include <nk/accessibility/accessible.h>
#include <nk/animation/easing.h>
#include <nk/animation/timed_animation.h>
#include <nk/accessibility/atspi_bridge.h>
#include <nk/accessibility/role.h>
#include <nk/actions/action.h>
#include <nk/actions/shortcut.h>
#include <nk/controllers/event_controller.h>
#include <nk/debug/diagnostics.h>
#include <nk/foundation/logging.h>
#include <nk/foundation/property.h>
#include <nk/foundation/result.h>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/layout/box_layout.h>
#include <nk/layout/constraints.h>
#include <nk/layout/grid_layout.h>
#include <nk/layout/layout_manager.h>
#include <nk/layout/stack_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/file_dialog.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/native_menu.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/spell_checker.h>
#include <nk/platform/system_preferences.h>
#include <nk/platform/window.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/runtime/event_loop.h>
#include <nk/style/style_class.h>
#include <nk/style/theme.h>
#include <nk/style/theme_selection.h>
#include <nk/text/font.h>
#include <nk/text/shaped_text.h>
#include <nk/text/text_shaper.h>
#include <nk/ui_core/cursor_shape.h>
#include <nk/ui_core/state_flags.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>

namespace {

void force_symbol_references() {
    (void)static_cast<nk::AccessibleRole (nk::Accessible::*)() const>(&nk::Accessible::role);
    (void)static_cast<void (nk::Accessible::*)(nk::AccessibleRole)>(&nk::Accessible::set_role);
    (void)static_cast<std::string_view (nk::Accessible::*)() const>(&nk::Accessible::name);
    (void)static_cast<void (nk::Accessible::*)(std::string)>(&nk::Accessible::set_name);
    (void)static_cast<std::string_view (nk::Accessible::*)() const>(&nk::Accessible::description);
    (void)static_cast<void (nk::Accessible::*)(std::string)>(&nk::Accessible::set_description);
    (void)static_cast<bool (nk::Accessible::*)() const>(&nk::Accessible::is_hidden);
    (void)static_cast<void (nk::Accessible::*)(bool)>(&nk::Accessible::set_hidden);
    (void)static_cast<nk::StateFlags (nk::Accessible::*)() const>(&nk::Accessible::state);
    (void)static_cast<void (nk::Accessible::*)(nk::StateFlags)>(&nk::Accessible::set_state);
    (void)static_cast<std::string_view (nk::Accessible::*)() const>(&nk::Accessible::value);
    (void)static_cast<void (nk::Accessible::*)(std::string)>(&nk::Accessible::set_value);
    (void)static_cast<void (nk::Accessible::*)(nk::AccessibleAction, std::function<bool()>)>(&nk::Accessible::add_action);
    (void)static_cast<void (nk::Accessible::*)(nk::AccessibleAction)>(&nk::Accessible::remove_action);
    (void)static_cast<bool (nk::Accessible::*)(nk::AccessibleAction) const>(&nk::Accessible::supports_action);
    (void)static_cast<std::span<const nk::AccessibleAction> (nk::Accessible::*)() const>(&nk::Accessible::actions);
    (void)static_cast<bool (nk::Accessible::*)(nk::AccessibleAction) const>(&nk::Accessible::perform_action);
    (void)static_cast<void (nk::Accessible::*)(nk::AccessibleRelationKind, std::string)>(&nk::Accessible::set_relation);
    (void)static_cast<void (nk::Accessible::*)(nk::AccessibleRelationKind)>(&nk::Accessible::remove_relation);
    (void)static_cast<std::span<const nk::AccessibleRelation> (nk::Accessible::*)() const>(&nk::Accessible::relations);
    (void)&nk::atspi_sanitize_object_name;
    (void)&nk::atspi_role_name;
    (void)&nk::atspi_state_bits;
    (void)&nk::build_atspi_window_snapshot;
    (void)&nk::find_atspi_accessible_node;
    (void)static_cast<std::string_view (nk::Action::*)() const>(&nk::Action::name);
    (void)static_cast<bool (nk::Action::*)() const>(&nk::Action::is_enabled);
    (void)static_cast<void (nk::Action::*)(bool)>(&nk::Action::set_enabled);
    (void)static_cast<void (nk::Action::*)()>(&nk::Action::activate);
    (void)static_cast<nk::Signal<>& (nk::Action::*)()>(&nk::Action::on_activated);
    (void)static_cast<void (nk::ActionGroup::*)(std::shared_ptr<nk::Action>)>(&nk::ActionGroup::add);
    (void)static_cast<nk::Action* (nk::ActionGroup::*)(std::string_view) const>(&nk::ActionGroup::lookup);
    (void)static_cast<void (nk::ActionGroup::*)(std::string_view)>(&nk::ActionGroup::activate);
    (void)static_cast<nk::Widget* (nk::EventController::*)() const>(&nk::EventController::widget);
    (void)static_cast<nk::Signal<float, float>& (nk::PointerController::*)()>(&nk::PointerController::on_enter);
    (void)static_cast<nk::Signal<>& (nk::PointerController::*)()>(&nk::PointerController::on_leave);
    (void)static_cast<nk::Signal<float, float>& (nk::PointerController::*)()>(&nk::PointerController::on_motion);
    (void)static_cast<nk::Signal<float, float, int>& (nk::PointerController::*)()>(&nk::PointerController::on_pressed);
    (void)static_cast<nk::Signal<float, float, int>& (nk::PointerController::*)()>(&nk::PointerController::on_released);
    (void)static_cast<nk::Signal<int, int>& (nk::KeyboardController::*)()>(&nk::KeyboardController::on_key_pressed);
    (void)static_cast<nk::Signal<int, int>& (nk::KeyboardController::*)()>(&nk::KeyboardController::on_key_released);
    (void)static_cast<nk::Signal<>& (nk::FocusController::*)()>(&nk::FocusController::on_focus_in);
    (void)static_cast<nk::Signal<>& (nk::FocusController::*)()>(&nk::FocusController::on_focus_out);
    (void)&nk::frame_request_reason_name;
    (void)&nk::gpu_present_path_name;
    (void)&nk::gpu_present_tradeoff_name;
    (void)&nk::has_frame_request_reason;
    (void)&nk::classify_frame_time;
    (void)&nk::build_frame_time_histogram;
    (void)&nk::count_render_snapshot_nodes;
    (void)&nk::build_render_snapshot;
    (void)&nk::format_widget_debug_tree;
    (void)&nk::format_widget_debug_json;
    (void)&nk::parse_widget_debug_json;
    (void)&nk::load_widget_debug_json_file;
    (void)&nk::save_widget_debug_json_file;
    (void)&nk::format_render_snapshot_tree;
    (void)&nk::format_render_snapshot_json;
    (void)&nk::parse_render_snapshot_json;
    (void)&nk::load_render_snapshot_json_file;
    (void)&nk::save_render_snapshot_json_file;
    (void)&nk::format_frame_diagnostics_artifact_json;
    (void)&nk::parse_frame_diagnostics_artifact_json;
    (void)&nk::load_frame_diagnostics_artifact_json_file;
    (void)&nk::save_frame_diagnostics_artifact_json_file;
    (void)&nk::format_frame_diagnostics_trace_json;
    (void)&nk::parse_frame_diagnostics_trace_json;
    (void)&nk::load_frame_diagnostics_trace_json_file;
    (void)&nk::save_frame_diagnostics_trace_json_file;
    (void)static_cast<int (nk::StyleSelector::*)() const>(&nk::StyleSelector::specificity);
    (void)&nk::set_log_level;
    (void)&nk::log_level;
    (void)&nk::log;
    (void)&nk::detail::should_log;
    (void)static_cast<void (nk::Connection::*)()>(&nk::Connection::disconnect);
    (void)static_cast<bool (nk::Connection::*)() const>(&nk::Connection::connected);
    (void)static_cast<nk::ScopedConnection& (nk::ScopedConnection::*)(nk::ScopedConnection&&)>(&nk::ScopedConnection::operator=);
    (void)static_cast<void (nk::ScopedConnection::*)()>(&nk::ScopedConnection::disconnect);
    (void)static_cast<nk::Connection (nk::ScopedConnection::*)()>(&nk::ScopedConnection::release);
    (void)static_cast<bool (nk::ScopedConnection::*)() const>(&nk::ScopedConnection::connected);
    (void)static_cast<float (nk::BoxLayout::*)() const>(&nk::BoxLayout::spacing);
    (void)static_cast<void (nk::BoxLayout::*)(float)>(&nk::BoxLayout::set_spacing);
    (void)static_cast<bool (nk::BoxLayout::*)() const>(&nk::BoxLayout::homogeneous);
    (void)static_cast<void (nk::BoxLayout::*)(bool)>(&nk::BoxLayout::set_homogeneous);
    (void)static_cast<nk::SizeRequest (nk::BoxLayout::*)(const nk::Widget&, const nk::Constraints&) const>(&nk::BoxLayout::measure);
    (void)static_cast<void (nk::BoxLayout::*)(nk::Widget&, const nk::Rect&)>(&nk::BoxLayout::allocate);
    (void)static_cast<float (nk::GridLayout::*)() const>(&nk::GridLayout::row_spacing);
    (void)static_cast<void (nk::GridLayout::*)(float)>(&nk::GridLayout::set_row_spacing);
    (void)static_cast<float (nk::GridLayout::*)() const>(&nk::GridLayout::column_spacing);
    (void)static_cast<void (nk::GridLayout::*)(float)>(&nk::GridLayout::set_column_spacing);
    (void)static_cast<void (nk::GridLayout::*)(nk::Widget&, int, int, int, int)>(&nk::GridLayout::attach);
    (void)static_cast<void (nk::GridLayout::*)(nk::Widget&)>(&nk::GridLayout::remove);
    (void)static_cast<void (nk::GridLayout::*)()>(&nk::GridLayout::clear);
    (void)static_cast<nk::SizeRequest (nk::GridLayout::*)(const nk::Widget&, const nk::Constraints&) const>(&nk::GridLayout::measure);
    (void)static_cast<void (nk::GridLayout::*)(nk::Widget&, const nk::Rect&)>(&nk::GridLayout::allocate);
    (void)static_cast<nk::SizeRequest (nk::StackLayout::*)(const nk::Widget&, const nk::Constraints&) const>(&nk::StackLayout::measure);
    (void)static_cast<void (nk::StackLayout::*)(nk::Widget&, const nk::Rect&)>(&nk::StackLayout::allocate);
    (void)static_cast<std::string (nk::AbstractListModel::*)(std::size_t) const>(&nk::AbstractListModel::display_text);
    (void)static_cast<nk::Signal<std::size_t, std::size_t>& (nk::AbstractListModel::*)()>(&nk::AbstractListModel::on_rows_about_to_insert);
    (void)static_cast<nk::Signal<std::size_t, std::size_t>& (nk::AbstractListModel::*)()>(&nk::AbstractListModel::on_rows_inserted);
    (void)static_cast<nk::Signal<std::size_t, std::size_t>& (nk::AbstractListModel::*)()>(&nk::AbstractListModel::on_rows_about_to_remove);
    (void)static_cast<nk::Signal<std::size_t, std::size_t>& (nk::AbstractListModel::*)()>(&nk::AbstractListModel::on_rows_removed);
    (void)static_cast<nk::Signal<std::size_t, std::size_t>& (nk::AbstractListModel::*)()>(&nk::AbstractListModel::on_data_changed);
    (void)static_cast<nk::Signal<>& (nk::AbstractListModel::*)()>(&nk::AbstractListModel::on_model_reset);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::AbstractListModel::*)(std::size_t, std::size_t)>(&nk::AbstractListModel::begin_insert_rows);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::AbstractListModel::*)()>(&nk::AbstractListModel::end_insert_rows);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::AbstractListModel::*)(std::size_t, std::size_t)>(&nk::AbstractListModel::begin_remove_rows);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::AbstractListModel::*)()>(&nk::AbstractListModel::end_remove_rows);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::AbstractListModel::*)(std::size_t, std::size_t)>(&nk::AbstractListModel::notify_data_changed);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::AbstractListModel::*)()>(&nk::AbstractListModel::notify_model_reset);
    (void)static_cast<std::size_t (nk::StringListModel::*)() const>(&nk::StringListModel::row_count);
    (void)static_cast<std::any (nk::StringListModel::*)(std::size_t) const>(&nk::StringListModel::data);
    (void)static_cast<std::string (nk::StringListModel::*)(std::size_t) const>(&nk::StringListModel::display_text);
    (void)static_cast<void (nk::StringListModel::*)(std::string)>(&nk::StringListModel::append);
    (void)static_cast<void (nk::StringListModel::*)(std::size_t, std::string)>(&nk::StringListModel::insert);
    (void)static_cast<void (nk::StringListModel::*)(std::size_t)>(&nk::StringListModel::remove);
    (void)static_cast<void (nk::StringListModel::*)()>(&nk::StringListModel::clear);
    (void)static_cast<const std::string& (nk::StringListModel::*)(std::size_t) const>(&nk::StringListModel::at);
    (void)static_cast<nk::SelectionMode (nk::SelectionModel::*)() const>(&nk::SelectionModel::mode);
    (void)static_cast<void (nk::SelectionModel::*)(nk::SelectionMode)>(&nk::SelectionModel::set_mode);
    (void)static_cast<void (nk::SelectionModel::*)(std::size_t)>(&nk::SelectionModel::select);
    (void)static_cast<void (nk::SelectionModel::*)(std::size_t)>(&nk::SelectionModel::deselect);
    (void)static_cast<void (nk::SelectionModel::*)(std::size_t)>(&nk::SelectionModel::toggle);
    (void)static_cast<void (nk::SelectionModel::*)()>(&nk::SelectionModel::clear);
    (void)static_cast<bool (nk::SelectionModel::*)(std::size_t) const>(&nk::SelectionModel::is_selected);
    (void)static_cast<const std::set<std::size_t>& (nk::SelectionModel::*)() const>(&nk::SelectionModel::selected_rows);
    (void)static_cast<std::size_t (nk::SelectionModel::*)() const>(&nk::SelectionModel::current_row);
    (void)static_cast<void (nk::SelectionModel::*)(std::size_t)>(&nk::SelectionModel::set_current_row);
    (void)static_cast<nk::Signal<>& (nk::SelectionModel::*)()>(&nk::SelectionModel::on_selection_changed);
    (void)static_cast<nk::Signal<std::size_t>& (nk::SelectionModel::*)()>(&nk::SelectionModel::on_current_changed);
    (void)static_cast<int (nk::Application::*)()>(&nk::Application::run);
    (void)static_cast<void (nk::Application::*)(int)>(&nk::Application::quit);
    (void)static_cast<nk::EventLoop& (nk::Application::*)()>(&nk::Application::event_loop);
    (void)static_cast<std::string_view (nk::Application::*)() const>(&nk::Application::app_id);
    (void)static_cast<std::string_view (nk::Application::*)() const>(&nk::Application::app_name);
    (void)static_cast<const nk::SystemPreferences& (nk::Application::*)() const>(&nk::Application::system_preferences);
    (void)static_cast<void (nk::Application::*)()>(&nk::Application::refresh_system_preferences);
    (void)static_cast<const nk::ThemeSelection& (nk::Application::*)() const>(&nk::Application::theme_selection);
    (void)static_cast<void (nk::Application::*)(nk::ThemeSelection)>(&nk::Application::set_theme_selection);
    (void)static_cast<void (nk::Application::*)(bool)>(&nk::Application::set_system_preferences_observation_enabled);
    (void)static_cast<bool (nk::Application::*)() const>(&nk::Application::is_system_preferences_observation_enabled);
    (void)static_cast<void (nk::Application::*)(std::chrono::milliseconds)>(&nk::Application::set_system_preferences_observation_interval);
    (void)static_cast<std::chrono::milliseconds (nk::Application::*)() const>(&nk::Application::system_preferences_observation_interval);
    (void)&nk::Application::instance;
    (void)static_cast<bool (nk::Application::*)() const>(&nk::Application::has_platform_backend);
    (void)static_cast<nk::PlatformBackend& (nk::Application::*)()>(&nk::Application::platform_backend);
    (void)static_cast<bool (nk::Application::*)() const>(&nk::Application::supports_open_file_dialog);
    (void)static_cast<nk::OpenFileDialogResult (nk::Application::*)(std::string_view, const std::vector<std::string>&)>(&nk::Application::open_file_dialog);
    (void)static_cast<bool (nk::Application::*)() const>(&nk::Application::supports_clipboard_text);
    (void)static_cast<std::string (nk::Application::*)() const>(&nk::Application::clipboard_text);
    (void)static_cast<void (nk::Application::*)(std::string)>(&nk::Application::set_clipboard_text);
    (void)static_cast<bool (nk::Application::*)() const>(&nk::Application::supports_primary_selection_text);
    (void)static_cast<std::string (nk::Application::*)() const>(&nk::Application::primary_selection_text);
    (void)static_cast<void (nk::Application::*)(std::string)>(&nk::Application::set_primary_selection_text);
    (void)static_cast<bool (nk::Application::*)() const>(&nk::Application::supports_native_app_menu);
    (void)static_cast<void (nk::Application::*)(std::vector<nk::NativeMenu>)>(&nk::Application::set_native_app_menu);
    (void)static_cast<nk::Signal<const nk::SystemPreferences&>& (nk::Application::*)()>(&nk::Application::on_system_preferences_changed);
    (void)static_cast<nk::Signal<const nk::ThemeSelection&>& (nk::Application::*)()>(&nk::Application::on_theme_selection_changed);
    (void)static_cast<nk::Signal<std::string_view>& (nk::Application::*)()>(&nk::Application::on_native_app_menu_action);
    (void)static_cast<void (nk::NativeSurface::*)()>(&nk::NativeSurface::show);
    (void)static_cast<void (nk::NativeSurface::*)()>(&nk::NativeSurface::hide);
    (void)static_cast<void (nk::NativeSurface::*)(std::string_view)>(&nk::NativeSurface::set_title);
    (void)static_cast<void (nk::NativeSurface::*)(int, int)>(&nk::NativeSurface::resize);
    (void)static_cast<nk::Size (nk::NativeSurface::*)() const>(&nk::NativeSurface::size);
    (void)static_cast<float (nk::NativeSurface::*)() const>(&nk::NativeSurface::scale_factor);
    (void)static_cast<void (nk::NativeSurface::*)(const uint8_t*, int, int, std::span<const nk::Rect>)>(&nk::NativeSurface::present);
    (void)static_cast<void (nk::NativeSurface::*)(bool)>(&nk::NativeSurface::set_fullscreen);
    (void)static_cast<bool (nk::NativeSurface::*)() const>(&nk::NativeSurface::is_fullscreen);
    (void)static_cast<nk::NativeWindowHandle (nk::NativeSurface::*)() const>(&nk::NativeSurface::native_handle);
    (void)static_cast<void (nk::NativeSurface::*)(nk::CursorShape)>(&nk::NativeSurface::set_cursor_shape);
    (void)static_cast<nk::Result<void> (nk::PlatformBackend::*)()>(&nk::PlatformBackend::initialize);
    (void)static_cast<void (nk::PlatformBackend::*)()>(&nk::PlatformBackend::shutdown);
    (void)static_cast<std::unique_ptr<nk::NativeSurface> (nk::PlatformBackend::*)(const nk::WindowConfig&, nk::Window&)>(&nk::PlatformBackend::create_surface);
    (void)static_cast<int (nk::PlatformBackend::*)(nk::EventLoop&)>(&nk::PlatformBackend::run_event_loop);
    (void)static_cast<void (nk::PlatformBackend::*)()>(&nk::PlatformBackend::wake_event_loop);
    (void)static_cast<void (nk::PlatformBackend::*)(int)>(&nk::PlatformBackend::request_quit);
    (void)static_cast<nk::OpenFileDialogResult (nk::PlatformBackend::*)(std::string_view, const std::vector<std::string>&)>(&nk::PlatformBackend::show_open_file_dialog);
    (void)static_cast<nk::SystemPreferences (nk::PlatformBackend::*)() const>(&nk::PlatformBackend::system_preferences);
    (void)&nk::PlatformBackend::create;
    (void)static_cast<std::string_view (*)(nk::PlatformFamily)>(&nk::to_string);
    (void)static_cast<std::string_view (*)(nk::DesktopEnvironment)>(&nk::to_string);
    (void)static_cast<void (nk::Window::*)(std::string_view)>(&nk::Window::set_title);
    (void)static_cast<std::string_view (nk::Window::*)() const>(&nk::Window::title);
    (void)static_cast<void (nk::Window::*)(int, int)>(&nk::Window::resize);
    (void)static_cast<nk::Size (nk::Window::*)() const>(&nk::Window::size);
    (void)static_cast<float (nk::Window::*)() const>(&nk::Window::scale_factor);
    (void)static_cast<nk::Size (nk::Window::*)() const>(&nk::Window::framebuffer_size);
    (void)static_cast<void (nk::Window::*)(std::shared_ptr<nk::Widget>)>(&nk::Window::set_child);
    (void)static_cast<nk::Widget* (nk::Window::*)() const>(&nk::Window::child);
    (void)static_cast<void (nk::Window::*)()>(&nk::Window::present);
    (void)static_cast<void (nk::Window::*)()>(&nk::Window::hide);
    (void)static_cast<void (nk::Window::*)()>(&nk::Window::close);
    (void)static_cast<bool (nk::Window::*)() const>(&nk::Window::is_visible);
    (void)static_cast<void (nk::Window::*)(bool)>(&nk::Window::set_fullscreen);
    (void)static_cast<bool (nk::Window::*)() const>(&nk::Window::is_fullscreen);
    (void)static_cast<void (nk::Window::*)(nk::TitlebarStyle)>(&nk::Window::set_titlebar_style);
    (void)static_cast<nk::TitlebarStyle (nk::Window::*)() const>(&nk::Window::titlebar_style);
    (void)static_cast<void (nk::Window::*)()>(&nk::Window::request_frame);
    (void)static_cast<void (nk::Window::*)()>(&nk::Window::invalidate_layout);
    (void)static_cast<void (nk::Window::*)(const nk::MouseEvent&)>(&nk::Window::dispatch_mouse_event);
    (void)static_cast<void (nk::Window::*)(const nk::KeyEvent&)>(&nk::Window::dispatch_key_event);
    (void)static_cast<void (nk::Window::*)(const nk::TextInputEvent&)>(&nk::Window::dispatch_text_input_event);
    (void)static_cast<void (nk::Window::*)(const nk::WindowEvent&)>(&nk::Window::dispatch_window_event);
    (void)static_cast<nk::NativeSurface* (nk::Window::*)() const>(&nk::Window::native_surface);
    (void)static_cast<nk::RendererBackend (nk::Window::*)() const>(&nk::Window::renderer_backend);
    (void)static_cast<nk::CursorShape (nk::Window::*)() const>(&nk::Window::current_cursor_shape);
    (void)static_cast<bool (nk::Window::*)(nk::KeyCode) const>(&nk::Window::is_key_pressed);
    (void)static_cast<std::optional<nk::Rect> (nk::Window::*)() const>(&nk::Window::current_text_input_caret_rect);
    (void)static_cast<std::optional<nk::WindowTextInputState> (nk::Window::*)() const>(&nk::Window::current_text_input_state);
    (void)static_cast<void (nk::Window::*)(nk::DebugOverlayFlags)>(&nk::Window::set_debug_overlay_flags);
    (void)static_cast<nk::DebugOverlayFlags (nk::Window::*)() const>(&nk::Window::debug_overlay_flags);
    (void)static_cast<void (nk::Window::*)(nk::DebugInspectorPresentation)>(&nk::Window::set_debug_inspector_presentation);
    (void)static_cast<nk::DebugInspectorPresentation (nk::Window::*)() const>(&nk::Window::debug_inspector_presentation);
    (void)static_cast<const nk::FrameDiagnostics& (nk::Window::*)() const>(&nk::Window::last_frame_diagnostics);
    (void)static_cast<std::span<const nk::FrameDiagnostics> (nk::Window::*)() const>(&nk::Window::debug_frame_history);
    (void)static_cast<nk::WidgetDebugNode (nk::Window::*)() const>(&nk::Window::debug_tree);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_widget_tree);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_widget_tree_json);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_widget_tree_json_file);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_frame_trace_json);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_frame_diagnostics_artifact_json_file);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_frame_trace_json_file);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_debug_bundle);
    (void)static_cast<std::vector<nk::TraceEvent> (nk::Window::*)() const>(&nk::Window::debug_selected_frame_runtime_events);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_selected_frame_summary);
    (void)static_cast<nk::Result<void> (nk::Window::*)() const>(&nk::Window::copy_selected_frame_summary_to_clipboard);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_selected_frame_summary_file);
    (void)static_cast<nk::RenderSnapshotNode (nk::Window::*)() const>(&nk::Window::debug_selected_frame_render_snapshot);
    (void)static_cast<nk::RenderSnapshotNode (nk::Window::*)() const>(&nk::Window::debug_selected_render_node);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_selected_frame_render_snapshot);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_selected_frame_render_snapshot_json);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_selected_render_node_details);
    (void)static_cast<nk::Result<void> (nk::Window::*)() const>(&nk::Window::copy_selected_render_node_details_to_clipboard);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_selected_render_node_details_file);
    (void)static_cast<void (nk::Window::*)(bool)>(&nk::Window::set_debug_picker_enabled);
    (void)static_cast<bool (nk::Window::*)() const>(&nk::Window::debug_picker_enabled);
    (void)static_cast<nk::Widget* (nk::Window::*)() const>(&nk::Window::debug_selected_widget);
    (void)static_cast<void (nk::Window::*)(nk::Widget*)>(&nk::Window::set_debug_selected_widget);
    (void)static_cast<void (nk::Window::*)(std::string_view)>(&nk::Window::set_debug_widget_filter);
    (void)static_cast<std::string_view (nk::Window::*)() const>(&nk::Window::debug_widget_filter);
    (void)static_cast<nk::WidgetDebugNode (nk::Window::*)() const>(&nk::Window::debug_selected_widget_info);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_selected_widget_details);
    (void)static_cast<std::string (nk::Window::*)() const>(&nk::Window::dump_selected_widget_details_json);
    (void)static_cast<nk::Result<void> (nk::Window::*)() const>(&nk::Window::copy_selected_widget_details_to_clipboard);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_selected_widget_details_file);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_selected_widget_details_json_file);
    (void)static_cast<nk::Result<void> (nk::Window::*)(std::string_view) const>(&nk::Window::save_debug_screenshot_ppm_file);
    (void)static_cast<nk::Signal<>& (nk::Window::*)()>(&nk::Window::on_close_requested);
    (void)static_cast<nk::Signal<int, int>& (nk::Window::*)()>(&nk::Window::on_resize);
    (void)static_cast<nk::Signal<float>& (nk::Window::*)()>(&nk::Window::on_scale_factor_changed);
    (void)static_cast<uint32_t const* (nk::ImageNode::*)() const>(&nk::ImageNode::pixel_data);
    (void)static_cast<int (nk::ImageNode::*)() const>(&nk::ImageNode::src_width);
    (void)static_cast<int (nk::ImageNode::*)() const>(&nk::ImageNode::src_height);
    (void)static_cast<nk::ScaleMode (nk::ImageNode::*)() const>(&nk::ImageNode::scale_mode);
    (void)static_cast<nk::RenderNodeKind (nk::RenderNode::*)() const>(&nk::RenderNode::kind);
    (void)static_cast<const nk::Rect& (nk::RenderNode::*)() const>(&nk::RenderNode::bounds);
    (void)static_cast<void (nk::RenderNode::*)(nk::Rect)>(&nk::RenderNode::set_bounds);
    (void)static_cast<void (nk::RenderNode::*)(std::string, std::span<const std::size_t>)>(&nk::RenderNode::set_debug_source);
    (void)static_cast<std::string_view (nk::RenderNode::*)() const>(&nk::RenderNode::debug_source_label);
    (void)static_cast<std::span<const std::size_t> (nk::RenderNode::*)() const>(&nk::RenderNode::debug_source_path);
    (void)static_cast<void (nk::RenderNode::*)(std::unique_ptr<nk::RenderNode>)>(&nk::RenderNode::append_child);
    (void)static_cast<const std::vector<std::unique_ptr<nk::RenderNode>>& (nk::RenderNode::*)() const>(&nk::RenderNode::children);
    (void)static_cast<nk::Color (nk::ColorRectNode::*)() const>(&nk::ColorRectNode::color);
    (void)static_cast<nk::Color (nk::RoundedRectNode::*)() const>(&nk::RoundedRectNode::color);
    (void)static_cast<float (nk::RoundedRectNode::*)() const>(&nk::RoundedRectNode::corner_radius);
    (void)static_cast<nk::Color (nk::BorderNode::*)() const>(&nk::BorderNode::color);
    (void)static_cast<float (nk::BorderNode::*)() const>(&nk::BorderNode::thickness);
    (void)static_cast<float (nk::BorderNode::*)() const>(&nk::BorderNode::corner_radius);
    (void)static_cast<float (nk::RoundedClipNode::*)() const>(&nk::RoundedClipNode::corner_radius);
    (void)static_cast<const std::string& (nk::TextNode::*)() const>(&nk::TextNode::text);
    (void)static_cast<nk::Color (nk::TextNode::*)() const>(&nk::TextNode::text_color);
    (void)static_cast<const nk::FontDescriptor& (nk::TextNode::*)() const>(&nk::TextNode::font);
    (void)static_cast<bool (nk::Renderer::*)(nk::NativeSurface&)>(&nk::Renderer::attach_surface);
    (void)static_cast<void (nk::Renderer::*)(nk::TextShaper*)>(&nk::Renderer::set_text_shaper);
    (void)static_cast<void (nk::Renderer::*)(std::span<const nk::Rect>)>(&nk::Renderer::set_damage_regions);
    (void)static_cast<nk::RenderHotspotCounters (nk::Renderer::*)() const>(&nk::Renderer::last_hotspot_counters);
    (void)static_cast<const uint8_t* (nk::SoftwareRenderer::*)() const>(&nk::SoftwareRenderer::pixel_data);
    (void)static_cast<int (nk::SoftwareRenderer::*)() const>(&nk::SoftwareRenderer::pixel_width);
    (void)static_cast<int (nk::SoftwareRenderer::*)() const>(&nk::SoftwareRenderer::pixel_height);
    (void)&nk::create_renderer;
    (void)static_cast<void (nk::SnapshotContext::*)(std::unique_ptr<nk::RenderNode>)>(&nk::SnapshotContext::add_node);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Rect, nk::Color)>(&nk::SnapshotContext::add_color_rect);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Rect, nk::Color, float)>(&nk::SnapshotContext::add_rounded_rect);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Rect, nk::Color, float, float)>(&nk::SnapshotContext::add_border);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Point, std::string, nk::Color, nk::FontDescriptor)>(&nk::SnapshotContext::add_text);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Rect, const uint32_t*, int, int, nk::ScaleMode)>(&nk::SnapshotContext::add_image);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Rect)>(&nk::SnapshotContext::push_container);
    (void)static_cast<void (nk::SnapshotContext::*)(bool)>(&nk::SnapshotContext::set_debug_annotations_enabled);
    (void)static_cast<bool (nk::SnapshotContext::*)() const>(&nk::SnapshotContext::debug_annotations_enabled);
    (void)static_cast<void (nk::SnapshotContext::*)(std::string, std::span<const std::size_t>)>(&nk::SnapshotContext::push_debug_source);
    (void)static_cast<void (nk::SnapshotContext::*)()>(&nk::SnapshotContext::pop_debug_source);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Rect, float)>(&nk::SnapshotContext::push_rounded_clip);
    (void)static_cast<void (nk::SnapshotContext::*)(nk::Rect)>(&nk::SnapshotContext::push_overlay_container);
    (void)static_cast<void (nk::SnapshotContext::*)()>(&nk::SnapshotContext::pop_container);
    (void)static_cast<std::unique_ptr<nk::RenderNode> (nk::SnapshotContext::*)()>(&nk::SnapshotContext::take_root);
    (void)static_cast<int (nk::EventLoop::*)()>(&nk::EventLoop::run);
    (void)static_cast<void (nk::EventLoop::*)(int)>(&nk::EventLoop::quit);
    (void)static_cast<void (nk::EventLoop::*)(std::function<void()>, std::string_view)>(&nk::EventLoop::post);
    (void)static_cast<nk::CallbackHandle (nk::EventLoop::*)(std::chrono::milliseconds, std::function<void()>, std::string_view)>(&nk::EventLoop::set_timeout);
    (void)static_cast<nk::CallbackHandle (nk::EventLoop::*)(std::chrono::milliseconds, std::function<void()>, std::string_view)>(&nk::EventLoop::set_interval);
    (void)static_cast<void (nk::EventLoop::*)(nk::CallbackHandle)>(&nk::EventLoop::cancel);
    (void)static_cast<nk::CallbackHandle (nk::EventLoop::*)(std::function<void()>, std::string_view)>(&nk::EventLoop::add_idle);
    (void)static_cast<bool (nk::EventLoop::*)()>(&nk::EventLoop::poll);
    (void)static_cast<void (nk::EventLoop::*)()>(&nk::EventLoop::wake);
    (void)static_cast<bool (nk::EventLoop::*)() const>(&nk::EventLoop::is_running);
    (void)static_cast<int (nk::EventLoop::*)() const>(&nk::EventLoop::exit_code);
    (void)static_cast<std::span<const nk::TraceEvent> (nk::EventLoop::*)() const>(&nk::EventLoop::debug_trace_events);
    (void)static_cast<void (nk::EventLoop::*)()>(&nk::EventLoop::clear_debug_trace_events);
    (void)static_cast<int (nk::StyleSelector::*)() const>(&nk::StyleSelector::specificity);
    (void)static_cast<std::string_view (nk::Theme::*)() const>(&nk::Theme::name);
    (void)static_cast<void (nk::Theme::*)(nk::StyleRule)>(&nk::Theme::add_rule);
    (void)static_cast<void (nk::Theme::*)(std::string, nk::StyleValue)>(&nk::Theme::set_token);
    (void)static_cast<const nk::StyleValue* (nk::Theme::*)(std::string_view) const>(&nk::Theme::token);
    (void)static_cast<const nk::StyleValue* (nk::Theme::*)(std::string_view, const std::vector<std::string>&, nk::StateFlags, std::string_view) const>(&nk::Theme::resolve);
    (void)&nk::Theme::set_active;
    (void)&nk::Theme::active;
    (void)&nk::Theme::make_light;
    (void)&nk::Theme::make_dark;
    (void)&nk::Theme::make_linux_gnome;
    (void)&nk::Theme::make_windows_11;
    (void)&nk::Theme::make_macos_26;
    (void)&nk::resolve_theme_selection;
    (void)&nk::make_theme;
    (void)&nk::default_theme_family_for;
    (void)static_cast<nk::ShapedText& (nk::ShapedText::*)(nk::ShapedText&&)>(&nk::ShapedText::operator=);
    (void)static_cast<nk::Size (nk::ShapedText::*)() const>(&nk::ShapedText::text_size);
    (void)static_cast<void (nk::ShapedText::*)(nk::Size)>(&nk::ShapedText::set_text_size);
    (void)static_cast<float (nk::ShapedText::*)() const>(&nk::ShapedText::baseline);
    (void)static_cast<void (nk::ShapedText::*)(float)>(&nk::ShapedText::set_baseline);
    (void)static_cast<uint8_t const* (nk::ShapedText::*)() const>(&nk::ShapedText::bitmap_data);
    (void)static_cast<int (nk::ShapedText::*)() const>(&nk::ShapedText::bitmap_width);
    (void)static_cast<int (nk::ShapedText::*)() const>(&nk::ShapedText::bitmap_height);
    (void)static_cast<void (nk::ShapedText::*)(std::vector<uint8_t>, int, int)>(&nk::ShapedText::set_bitmap);
    (void)&nk::TextShaper::create;
    (void)static_cast<nk::Widget* (nk::Widget::*)() const>(&nk::Widget::parent);
    (void)static_cast<std::span<const std::shared_ptr<nk::Widget>> (nk::Widget::*)() const>(&nk::Widget::children);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::is_visible);
    (void)static_cast<void (nk::Widget::*)(bool)>(&nk::Widget::set_visible);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::is_sensitive);
    (void)static_cast<void (nk::Widget::*)(bool)>(&nk::Widget::set_sensitive);
    (void)static_cast<nk::StateFlags (nk::Widget::*)() const>(&nk::Widget::state_flags);
    (void)static_cast<void (nk::Widget::*)(std::string_view)>(&nk::Widget::add_style_class);
    (void)static_cast<void (nk::Widget::*)(std::string_view)>(&nk::Widget::remove_style_class);
    (void)static_cast<bool (nk::Widget::*)(std::string_view) const>(&nk::Widget::has_style_class);
    (void)static_cast<std::span<const std::string> (nk::Widget::*)() const>(&nk::Widget::style_classes);
    (void)static_cast<void (nk::Widget::*)(std::string_view)>(&nk::Widget::set_debug_name);
    (void)static_cast<std::string_view (nk::Widget::*)() const>(&nk::Widget::debug_name);
    (void)static_cast<nk::SizeRequest (nk::Widget::*)(const nk::Constraints&) const>(&nk::Widget::measure);
    (void)static_cast<nk::SizeRequest (nk::Widget::*)(const nk::Constraints&) const>(&nk::Widget::measure_for_diagnostics);
    (void)static_cast<void (nk::Widget::*)(const nk::Rect&)>(&nk::Widget::allocate);
    (void)static_cast<const nk::Rect& (nk::Widget::*)() const>(&nk::Widget::allocation);
    (void)static_cast<void (nk::Widget::*)(std::unique_ptr<nk::LayoutManager>)>(&nk::Widget::set_layout_manager);
    (void)static_cast<nk::LayoutManager* (nk::Widget::*)() const>(&nk::Widget::layout_manager);
    (void)static_cast<nk::SizePolicy (nk::Widget::*)() const>(&nk::Widget::horizontal_size_policy);
    (void)static_cast<void (nk::Widget::*)(nk::SizePolicy)>(&nk::Widget::set_horizontal_size_policy);
    (void)static_cast<nk::SizePolicy (nk::Widget::*)() const>(&nk::Widget::vertical_size_policy);
    (void)static_cast<void (nk::Widget::*)(nk::SizePolicy)>(&nk::Widget::set_vertical_size_policy);
    (void)static_cast<uint8_t (nk::Widget::*)() const>(&nk::Widget::horizontal_stretch);
    (void)static_cast<void (nk::Widget::*)(uint8_t)>(&nk::Widget::set_horizontal_stretch);
    (void)static_cast<uint8_t (nk::Widget::*)() const>(&nk::Widget::vertical_stretch);
    (void)static_cast<void (nk::Widget::*)(uint8_t)>(&nk::Widget::set_vertical_stretch);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::retain_size_when_hidden);
    (void)static_cast<void (nk::Widget::*)(bool)>(&nk::Widget::set_retain_size_when_hidden);
    (void)static_cast<nk::Insets (nk::Widget::*)() const>(&nk::Widget::margin);
    (void)static_cast<void (nk::Widget::*)(nk::Insets)>(&nk::Widget::set_margin);
    (void)static_cast<nk::Insets (nk::Widget::*)() const>(&nk::Widget::padding);
    (void)static_cast<void (nk::Widget::*)(nk::Insets)>(&nk::Widget::set_padding);
    (void)static_cast<nk::Rect (nk::Widget::*)() const>(&nk::Widget::content_rect);
    (void)static_cast<float (nk::OpacityNode::*)() const>(&nk::OpacityNode::opacity);
    (void)static_cast<nk::Color (nk::LinearGradientNode::*)() const>(&nk::LinearGradientNode::start_color);
    (void)static_cast<nk::Color (nk::LinearGradientNode::*)() const>(&nk::LinearGradientNode::end_color);
    (void)static_cast<nk::Orientation (nk::LinearGradientNode::*)() const>(&nk::LinearGradientNode::direction);
    (void)static_cast<nk::Color (nk::ShadowNode::*)() const>(&nk::ShadowNode::color);
    (void)static_cast<float (nk::ShadowNode::*)() const>(&nk::ShadowNode::offset_x);
    (void)static_cast<float (nk::ShadowNode::*)() const>(&nk::ShadowNode::offset_y);
    (void)static_cast<float (nk::ShadowNode::*)() const>(&nk::ShadowNode::blur_radius);
    (void)static_cast<float (nk::ShadowNode::*)() const>(&nk::ShadowNode::spread);
    (void)static_cast<float (nk::ShadowNode::*)() const>(&nk::ShadowNode::corner_radius);

    // animation
    (void)&nk::easing::linear;
    (void)&nk::easing::ease_in;
    (void)&nk::easing::ease_out;
    (void)&nk::easing::ease_in_out;
    (void)static_cast<void (nk::TimedAnimation::*)()>(&nk::TimedAnimation::start);
    (void)static_cast<void (nk::TimedAnimation::*)()>(&nk::TimedAnimation::stop);
    (void)static_cast<bool (nk::TimedAnimation::*)() const>(&nk::TimedAnimation::is_running);
    (void)static_cast<bool (nk::TimedAnimation::*)() const>(&nk::TimedAnimation::is_finished);
    (void)static_cast<float (nk::TimedAnimation::*)() const>(&nk::TimedAnimation::value);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::is_focusable);
    (void)static_cast<void (nk::Widget::*)(bool)>(&nk::Widget::set_focusable);
    (void)static_cast<void (nk::Widget::*)()>(&nk::Widget::grab_focus);
    (void)static_cast<nk::Accessible* (nk::Widget::*)()>(&nk::Widget::accessible);
    (void)static_cast<const nk::Accessible* (nk::Widget::*)() const>(&nk::Widget::accessible);
    (void)static_cast<nk::Accessible& (nk::Widget::*)()>(&nk::Widget::ensure_accessible);
    (void)static_cast<nk::WidgetHotspotCounters (nk::Widget::*)() const>(&nk::Widget::debug_hotspot_counters);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::debug_pending_redraw);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::debug_pending_layout);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::debug_has_last_measure);
    (void)static_cast<nk::Constraints (nk::Widget::*)() const>(&nk::Widget::debug_last_measure_constraints);
    (void)static_cast<nk::SizeRequest (nk::Widget::*)() const>(&nk::Widget::debug_last_size_request);
    (void)static_cast<std::vector<nk::Rect> (nk::Widget::*)() const>(&nk::Widget::debug_damage_regions);
    (void)static_cast<std::span<const nk::Rect> (nk::Widget::*)() const>(&nk::Widget::debug_preserved_damage_regions);
    (void)static_cast<bool (nk::Widget::*)() const>(&nk::Widget::debug_has_previous_allocation);
    (void)static_cast<nk::Rect (nk::Widget::*)() const>(&nk::Widget::debug_previous_allocation);
    (void)static_cast<void (nk::Widget::*)()>(&nk::Widget::queue_redraw);
    (void)static_cast<void (nk::Widget::*)(nk::Rect)>(&nk::Widget::queue_redraw);
    (void)static_cast<void (nk::Widget::*)()>(&nk::Widget::queue_layout);
    (void)static_cast<void (nk::Widget::*)(std::shared_ptr<nk::EventController>)>(&nk::Widget::add_controller);
    (void)static_cast<nk::Signal<>& (nk::Widget::*)()>(&nk::Widget::on_map);
    (void)static_cast<nk::Signal<>& (nk::Widget::*)()>(&nk::Widget::on_unmap);
    (void)static_cast<nk::Signal<>& (nk::Widget::*)()>(&nk::Widget::on_destroy);
    (void)static_cast<void (nk::Widget::*)(nk::SnapshotContext&) const>(&nk::Widget::snapshot);
    (void)static_cast<bool (nk::Widget::*)(const nk::MouseEvent&)>(&nk::Widget::handle_mouse_event);
    (void)static_cast<bool (nk::Widget::*)(const nk::KeyEvent&)>(&nk::Widget::handle_key_event);
    (void)static_cast<bool (nk::Widget::*)(const nk::TextInputEvent&)>(&nk::Widget::handle_text_input_event);
    (void)static_cast<bool (nk::Widget::*)(nk::Point) const>(&nk::Widget::hit_test);
    (void)static_cast<std::vector<nk::Rect> (nk::Widget::*)() const>(&nk::Widget::damage_regions);
    (void)static_cast<nk::CursorShape (nk::Widget::*)() const>(&nk::Widget::cursor_shape);
    (void)static_cast<void (nk::Widget::*)(bool)>(&nk::Widget::on_focus_changed);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<nk::Color (nk::Widget::*)(std::string_view, nk::Color) const>(&nk::Widget::theme_color);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<float (nk::Widget::*)(std::string_view, float) const>(&nk::Widget::theme_number);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<nk::Size (nk::Widget::*)(std::string_view, const nk::FontDescriptor&) const>(&nk::Widget::measure_text);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)() const>(&nk::Widget::note_text_measure_for_diagnostics);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)() const>(&nk::Widget::note_image_snapshot_for_diagnostics);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)(std::size_t, std::size_t, std::size_t) const>(&nk::Widget::note_model_view_sync_for_diagnostics);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)()>(&nk::Widget::preserve_damage_regions_for_next_redraw);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)(std::shared_ptr<nk::Widget>)>(&nk::Widget::append_child);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)(std::size_t, std::shared_ptr<nk::Widget>)>(&nk::Widget::insert_child);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)(nk::Widget&)>(&nk::Widget::remove_child);
    // REMOVED (protected member, caught by audit iteration): (void)static_cast<void (nk::Widget::*)()>(&nk::Widget::clear_children);
    // REMOVED (protected): set_state_flag
    (void)&nk::Button::create;
    (void)static_cast<std::string_view (nk::Button::*)() const>(&nk::Button::label);
    (void)static_cast<void (nk::Button::*)(std::string)>(&nk::Button::set_label);
    (void)static_cast<nk::Signal<>& (nk::Button::*)()>(&nk::Button::on_clicked);
    (void)static_cast<nk::SizeRequest (nk::Button::*)(const nk::Constraints&) const>(&nk::Button::measure);
    (void)static_cast<bool (nk::Button::*)(const nk::MouseEvent&)>(&nk::Button::handle_mouse_event);
    (void)static_cast<bool (nk::Button::*)(const nk::KeyEvent&)>(&nk::Button::handle_key_event);
    (void)static_cast<nk::CursorShape (nk::Button::*)() const>(&nk::Button::cursor_shape);
    (void)&nk::ComboBox::create;
    (void)static_cast<void (nk::ComboBox::*)(std::vector<std::string>)>(&nk::ComboBox::set_items);
    (void)static_cast<std::size_t (nk::ComboBox::*)() const>(&nk::ComboBox::item_count);
    (void)static_cast<std::string_view (nk::ComboBox::*)(std::size_t) const>(&nk::ComboBox::item);
    (void)static_cast<int (nk::ComboBox::*)() const>(&nk::ComboBox::selected_index);
    (void)static_cast<void (nk::ComboBox::*)(int)>(&nk::ComboBox::set_selected_index);
    (void)static_cast<std::string_view (nk::ComboBox::*)() const>(&nk::ComboBox::selected_text);
    (void)static_cast<nk::Signal<int>& (nk::ComboBox::*)()>(&nk::ComboBox::on_selection_changed);
    (void)static_cast<nk::SizeRequest (nk::ComboBox::*)(const nk::Constraints&) const>(&nk::ComboBox::measure);
    (void)static_cast<bool (nk::ComboBox::*)(const nk::MouseEvent&)>(&nk::ComboBox::handle_mouse_event);
    (void)static_cast<bool (nk::ComboBox::*)(const nk::KeyEvent&)>(&nk::ComboBox::handle_key_event);
    (void)static_cast<bool (nk::ComboBox::*)(nk::Point) const>(&nk::ComboBox::hit_test);
    (void)static_cast<std::vector<nk::Rect> (nk::ComboBox::*)() const>(&nk::ComboBox::damage_regions);
    (void)static_cast<nk::CursorShape (nk::ComboBox::*)() const>(&nk::ComboBox::cursor_shape);
    (void)static_cast<void (nk::ComboBox::*)(bool)>(&nk::ComboBox::on_focus_changed);
    (void)&nk::Dialog::create;
    (void)static_cast<void (nk::Dialog::*)(std::string, nk::DialogResponse)>(&nk::Dialog::add_button);
    (void)static_cast<void (nk::Dialog::*)(std::shared_ptr<nk::Widget>)>(&nk::Dialog::set_content);
    (void)static_cast<void (nk::Dialog::*)(nk::DialogPresentationStyle)>(&nk::Dialog::set_presentation_style);
    (void)static_cast<void (nk::Dialog::*)(float)>(&nk::Dialog::set_minimum_panel_width);
    (void)static_cast<void (nk::Dialog::*)(nk::Window&)>(&nk::Dialog::present);
    (void)static_cast<bool (nk::Dialog::*)() const>(&nk::Dialog::is_presented);
    (void)static_cast<void (nk::Dialog::*)(nk::DialogResponse)>(&nk::Dialog::close);
    (void)static_cast<nk::Signal<nk::DialogResponse>& (nk::Dialog::*)()>(&nk::Dialog::on_response);
    (void)static_cast<nk::SizeRequest (nk::Dialog::*)(const nk::Constraints&) const>(&nk::Dialog::measure);
    (void)static_cast<void (nk::Dialog::*)(const nk::Rect&)>(&nk::Dialog::allocate);
    (void)static_cast<bool (nk::Dialog::*)(const nk::MouseEvent&)>(&nk::Dialog::handle_mouse_event);
    (void)static_cast<bool (nk::Dialog::*)(const nk::KeyEvent&)>(&nk::Dialog::handle_key_event);
    (void)static_cast<bool (nk::Dialog::*)(nk::Point) const>(&nk::Dialog::hit_test);
    (void)static_cast<std::vector<nk::Rect> (nk::Dialog::*)() const>(&nk::Dialog::damage_regions);
    (void)&nk::ImageView::create;
    (void)static_cast<void (nk::ImageView::*)(uint32_t const*, int, int)>(&nk::ImageView::update_pixel_buffer);
    (void)static_cast<void (nk::ImageView::*)(nk::ScaleMode)>(&nk::ImageView::set_scale_mode);
    (void)static_cast<nk::ScaleMode (nk::ImageView::*)() const>(&nk::ImageView::scale_mode);
    (void)static_cast<void (nk::ImageView::*)(bool)>(&nk::ImageView::set_preserve_aspect_ratio);
    (void)static_cast<bool (nk::ImageView::*)() const>(&nk::ImageView::preserve_aspect_ratio);
    (void)static_cast<int (nk::ImageView::*)() const>(&nk::ImageView::source_width);
    (void)static_cast<int (nk::ImageView::*)() const>(&nk::ImageView::source_height);
    (void)static_cast<nk::SizeRequest (nk::ImageView::*)(const nk::Constraints&) const>(&nk::ImageView::measure);
    (void)&nk::Label::create;
    (void)static_cast<std::string_view (nk::Label::*)() const>(&nk::Label::text);
    (void)static_cast<void (nk::Label::*)(std::string)>(&nk::Label::set_text);
    (void)static_cast<nk::HAlign (nk::Label::*)() const>(&nk::Label::h_align);
    (void)static_cast<void (nk::Label::*)(nk::HAlign)>(&nk::Label::set_h_align);
    (void)static_cast<nk::SizeRequest (nk::Label::*)(const nk::Constraints&) const>(&nk::Label::measure);
    (void)&nk::ListView::create;
    (void)static_cast<void (nk::ListView::*)(std::shared_ptr<nk::AbstractListModel>)>(&nk::ListView::set_model);
    (void)static_cast<nk::AbstractListModel* (nk::ListView::*)() const>(&nk::ListView::model);
    (void)static_cast<void (nk::ListView::*)(std::shared_ptr<nk::SelectionModel>)>(&nk::ListView::set_selection_model);
    (void)static_cast<nk::SelectionModel* (nk::ListView::*)() const>(&nk::ListView::selection_model);
    (void)static_cast<void (nk::ListView::*)(nk::ItemFactory)>(&nk::ListView::set_item_factory);
    (void)static_cast<void (nk::ListView::*)(float)>(&nk::ListView::set_row_height);
    (void)static_cast<nk::Signal<std::size_t>& (nk::ListView::*)()>(&nk::ListView::on_row_activated);
    (void)static_cast<nk::SizeRequest (nk::ListView::*)(const nk::Constraints&) const>(&nk::ListView::measure);
    (void)static_cast<void (nk::ListView::*)(const nk::Rect&)>(&nk::ListView::allocate);
    (void)static_cast<bool (nk::ListView::*)(const nk::MouseEvent&)>(&nk::ListView::handle_mouse_event);
    (void)static_cast<bool (nk::ListView::*)(const nk::KeyEvent&)>(&nk::ListView::handle_key_event);
    (void)&nk::MenuBar::create;
    (void)static_cast<void (nk::MenuBar::*)(nk::Menu)>(&nk::MenuBar::add_menu);
    (void)static_cast<void (nk::MenuBar::*)()>(&nk::MenuBar::clear);
    (void)static_cast<nk::Signal<std::string_view>& (nk::MenuBar::*)()>(&nk::MenuBar::on_action);
    (void)static_cast<nk::SizeRequest (nk::MenuBar::*)(const nk::Constraints&) const>(&nk::MenuBar::measure);
    (void)static_cast<bool (nk::MenuBar::*)(const nk::MouseEvent&)>(&nk::MenuBar::handle_mouse_event);
    (void)static_cast<bool (nk::MenuBar::*)(const nk::KeyEvent&)>(&nk::MenuBar::handle_key_event);
    (void)static_cast<bool (nk::MenuBar::*)(nk::Point) const>(&nk::MenuBar::hit_test);
    (void)static_cast<std::vector<nk::Rect> (nk::MenuBar::*)() const>(&nk::MenuBar::damage_regions);
    (void)static_cast<nk::CursorShape (nk::MenuBar::*)() const>(&nk::MenuBar::cursor_shape);
    (void)static_cast<void (nk::MenuBar::*)(bool)>(&nk::MenuBar::on_focus_changed);
    (void)&nk::ScrollArea::create;
    (void)static_cast<void (nk::ScrollArea::*)(std::shared_ptr<nk::Widget>)>(&nk::ScrollArea::set_content);
    (void)static_cast<nk::Widget* (nk::ScrollArea::*)() const>(&nk::ScrollArea::content);
    (void)static_cast<void (nk::ScrollArea::*)(nk::ScrollPolicy)>(&nk::ScrollArea::set_h_scroll_policy);
    (void)static_cast<void (nk::ScrollArea::*)(nk::ScrollPolicy)>(&nk::ScrollArea::set_v_scroll_policy);
    (void)static_cast<float (nk::ScrollArea::*)() const>(&nk::ScrollArea::h_offset);
    (void)static_cast<float (nk::ScrollArea::*)() const>(&nk::ScrollArea::v_offset);
    (void)static_cast<void (nk::ScrollArea::*)(float, float)>(&nk::ScrollArea::scroll_to);
    (void)static_cast<nk::Signal<float, float>& (nk::ScrollArea::*)()>(&nk::ScrollArea::on_scroll_changed);
    (void)static_cast<nk::SizeRequest (nk::ScrollArea::*)(const nk::Constraints&) const>(&nk::ScrollArea::measure);
    (void)static_cast<void (nk::ScrollArea::*)(const nk::Rect&)>(&nk::ScrollArea::allocate);
    (void)static_cast<bool (nk::ScrollArea::*)(const nk::MouseEvent&)>(&nk::ScrollArea::handle_mouse_event);
    (void)&nk::SegmentedControl::create;
    (void)static_cast<void (nk::SegmentedControl::*)(std::vector<std::string>)>(&nk::SegmentedControl::set_segments);
    (void)static_cast<std::size_t (nk::SegmentedControl::*)() const>(&nk::SegmentedControl::segment_count);
    (void)static_cast<std::string_view (nk::SegmentedControl::*)(std::size_t) const>(&nk::SegmentedControl::segment);
    (void)static_cast<int (nk::SegmentedControl::*)() const>(&nk::SegmentedControl::selected_index);
    (void)static_cast<void (nk::SegmentedControl::*)(int)>(&nk::SegmentedControl::set_selected_index);
    (void)static_cast<std::string_view (nk::SegmentedControl::*)() const>(&nk::SegmentedControl::selected_text);
    (void)static_cast<nk::Signal<int>& (nk::SegmentedControl::*)()>(&nk::SegmentedControl::on_selection_changed);
    (void)static_cast<nk::SizeRequest (nk::SegmentedControl::*)(const nk::Constraints&) const>(&nk::SegmentedControl::measure);
    (void)static_cast<bool (nk::SegmentedControl::*)(const nk::MouseEvent&)>(&nk::SegmentedControl::handle_mouse_event);
    (void)static_cast<bool (nk::SegmentedControl::*)(const nk::KeyEvent&)>(&nk::SegmentedControl::handle_key_event);
    (void)static_cast<nk::CursorShape (nk::SegmentedControl::*)() const>(&nk::SegmentedControl::cursor_shape);
    (void)static_cast<void (nk::SegmentedControl::*)(bool)>(&nk::SegmentedControl::on_focus_changed);
    (void)&nk::StatusBar::create;
    (void)static_cast<void (nk::StatusBar::*)(std::vector<std::string>)>(&nk::StatusBar::set_segments);
    (void)static_cast<void (nk::StatusBar::*)(std::size_t, std::string)>(&nk::StatusBar::set_segment);
    (void)static_cast<std::size_t (nk::StatusBar::*)() const>(&nk::StatusBar::segment_count);
    (void)static_cast<std::string_view (nk::StatusBar::*)(std::size_t) const>(&nk::StatusBar::segment);
    (void)static_cast<nk::SizeRequest (nk::StatusBar::*)(const nk::Constraints&) const>(&nk::StatusBar::measure);
    (void)&nk::TextField::create;
    (void)static_cast<std::string_view (nk::TextField::*)() const>(&nk::TextField::text);
    (void)static_cast<void (nk::TextField::*)(std::string)>(&nk::TextField::set_text);
    (void)static_cast<std::string_view (nk::TextField::*)() const>(&nk::TextField::placeholder);
    (void)static_cast<void (nk::TextField::*)(std::string)>(&nk::TextField::set_placeholder);
    (void)static_cast<bool (nk::TextField::*)() const>(&nk::TextField::is_editable);
    (void)static_cast<void (nk::TextField::*)(bool)>(&nk::TextField::set_editable);
    (void)static_cast<std::size_t (nk::TextField::*)() const>(&nk::TextField::cursor_position);
    (void)static_cast<std::size_t (nk::TextField::*)() const>(&nk::TextField::selection_start);
    (void)static_cast<std::size_t (nk::TextField::*)() const>(&nk::TextField::selection_end);
    (void)static_cast<bool (nk::TextField::*)() const>(&nk::TextField::has_selection);
    (void)static_cast<void (nk::TextField::*)()>(&nk::TextField::select_all);
    (void)static_cast<void (nk::TextField::*)(bool)>(&nk::TextField::set_spell_check_enabled);
    (void)static_cast<bool (nk::TextField::*)() const>(&nk::TextField::is_spell_check_enabled);
    (void)static_cast<nk::SpellChecker* (nk::PlatformBackend::*)()>(&nk::PlatformBackend::spell_checker);
    (void)static_cast<nk::Signal<std::string_view>& (nk::TextField::*)()>(&nk::TextField::on_text_changed);
    (void)static_cast<nk::Signal<>& (nk::TextField::*)()>(&nk::TextField::on_activate);
    (void)static_cast<nk::SizeRequest (nk::TextField::*)(const nk::Constraints&) const>(&nk::TextField::measure);
    (void)static_cast<void (nk::TextField::*)(const nk::Rect&)>(&nk::TextField::allocate);
    (void)static_cast<bool (nk::TextField::*)(const nk::MouseEvent&)>(&nk::TextField::handle_mouse_event);
    (void)static_cast<bool (nk::TextField::*)(const nk::KeyEvent&)>(&nk::TextField::handle_key_event);
    (void)static_cast<bool (nk::TextField::*)(const nk::TextInputEvent&)>(&nk::TextField::handle_text_input_event);
    (void)static_cast<std::optional<nk::Rect> (nk::TextField::*)() const>(&nk::TextField::text_input_caret_rect);
    (void)static_cast<nk::CursorShape (nk::TextField::*)() const>(&nk::TextField::cursor_shape);
    (void)static_cast<void (nk::TextField::*)(bool)>(&nk::TextField::on_focus_changed);
}

} // namespace

int main() {
    // Reference the function without calling it so the linker must resolve
    // its body without any runtime dependency on Application/Window.
    (void)&force_symbol_references;
    return 0;
}
