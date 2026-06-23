/// @file long_task.cpp
/// @brief Long-running task pattern: app-owned worker with UI-thread progress updates.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>
#include <nk/widgets/progress_bar.h>
#include <nk/widgets/status_bar.h>
#include <string>
#include <thread>

class Box : public nk::Widget {
public:
    static std::shared_ptr<Box> vertical(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

private:
    Box() = default;
};

struct TaskState {
    std::atomic<bool> cancel_requested = false;
    std::atomic<bool> running = false;
    std::thread worker;

    ~TaskState() {
        cancel_requested.store(true, std::memory_order_relaxed);
        if (worker.joinable()) {
            worker.join();
        }
    }
};

void start_worker(nk::Application& app,
                  std::shared_ptr<TaskState> state,
                  std::shared_ptr<nk::Label> summary,
                  std::shared_ptr<nk::ProgressBar> progress,
                  std::shared_ptr<nk::StatusBar> status,
                  std::shared_ptr<nk::Button> start_button,
                  std::shared_ptr<nk::Button> cancel_button) {
    if (state->running.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    if (state->worker.joinable()) {
        state->worker.join();
    }

    state->cancel_requested.store(false, std::memory_order_relaxed);
    summary->set_text("Preparing analysis...");
    progress->set_fraction(0.0F);
    status->set_segments({"Running", "0%"});
    start_button->set_label("Running");
    cancel_button->set_label("Cancel");

    state->worker =
        std::thread([&app, state, summary, progress, status, start_button, cancel_button] {
            constexpr int total_steps = 20;
            for (int step = 1; step <= total_steps; ++step) {
                if (state->cancel_requested.load(std::memory_order_relaxed)) {
                    app.event_loop().post(
                        [state, summary, progress, status, start_button, cancel_button] {
                            state->running.store(false, std::memory_order_relaxed);
                            summary->set_text("Analysis cancelled.");
                            progress->set_fraction(0.0F);
                            status->set_segments({"Cancelled", "Ready"});
                            start_button->set_label("Start analysis");
                            cancel_button->set_label("Cancel");
                        },
                        "long-task-cancelled");
                    return;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                const float fraction = static_cast<float>(step) / static_cast<float>(total_steps);
                const int percent = std::clamp(static_cast<int>(fraction * 100.0F), 0, 100);

                app.event_loop().post(
                    [summary, progress, status, percent, fraction] {
                        summary->set_text("Analyzing waveform chunk " + std::to_string(percent) +
                                          "%");
                        progress->set_fraction(fraction);
                        status->set_segments({"Running", std::to_string(percent) + "%"});
                    },
                    "long-task-progress");
            }

            app.event_loop().post(
                [&app, state, summary, progress, status, start_button, cancel_button] {
                    state->running.store(false, std::memory_order_relaxed);
                    summary->set_text("Analysis complete.");
                    progress->set_fraction(1.0F);
                    status->set_segments({"Complete", "100%"});
                    start_button->set_label("Start analysis");
                    cancel_button->set_label("Cancel");
                    app.quit(0);
                },
                "long-task-complete");
        });
}

int main(int argc, char** argv) {
    nk::Application app(argc, argv);

    nk::Window window({
        .title = "Long Task Pattern",
        .width = 480,
        .height = 240,
    });

    auto state = std::make_shared<TaskState>();
    auto root = Box::vertical(10.0F);
    auto title = nk::Label::create("Background analysis");
    auto summary = nk::Label::create("Ready.");
    auto progress = nk::ProgressBar::create();
    auto start_button = nk::Button::create("Start analysis");
    auto cancel_button = nk::Button::create("Cancel");
    auto status = nk::StatusBar::create();
    status->set_segments({"Ready", "0%"});

    auto start_conn = start_button->on_clicked().connect(
        [&] { start_worker(app, state, summary, progress, status, start_button, cancel_button); });
    auto cancel_conn = cancel_button->on_clicked().connect(
        [state] { state->cancel_requested.store(true, std::memory_order_relaxed); });
    (void)start_conn;
    (void)cancel_conn;

    root->append(title);
    root->append(summary);
    root->append(progress);
    root->append(start_button);
    root->append(cancel_button);
    root->append(status);

    window.set_child(root);
    window.present();

    app.event_loop().post(
        [&] { start_worker(app, state, summary, progress, status, start_button, cancel_button); },
        "long-task-auto-start");

    return app.run();
}
