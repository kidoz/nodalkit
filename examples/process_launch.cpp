/// @file process_launch.cpp
/// @brief Launch and monitor an external process without blocking the UI.
///
/// NodalKit does not ship a process API (see docs/PROCESS_LAUNCH.md for the
/// rationale). The supported pattern, demonstrated here, is: own the child
/// process in application code, drive it from a worker thread, and marshal every
/// UI-affecting result back onto the event-loop thread with EventLoop::post().
///
/// This is exactly the shape an emulator frontend needs: press Start, keep the
/// window responsive while the guest runs, stream status, allow cancellation via
/// terminate, and report the exit code (or launch error) when it ends.
///
/// The `ChildProcess` helper below is intentionally self-contained (not part of
/// NodalKit) so applications can copy and adapt it. It exposes what request §3
/// asks for: start, poll for exit, exit code, PID, and cancellation/termination.

#include <atomic>
#include <chrono>
#include <memory>
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>
#include <nk/widgets/status_bar.h>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

// --- Minimal, app-owned child-process wrapper -------------------------------

class ChildProcess {
public:
    /// Launch @p argv[0] with the given arguments. Returns true on success;
    /// on failure, error() explains why.
    bool start(const std::vector<std::string>& argv);

    /// Non-blocking check. Returns true once the child has exited, writing its
    /// code to @p exit_code. Returns false while it is still running.
    bool try_wait(int& exit_code);

    /// Ask the OS to terminate the child (SIGTERM / TerminateProcess).
    void terminate();

    [[nodiscard]] unsigned long long pid() const { return pid_; }

    [[nodiscard]] const std::string& error() const { return error_; }

    ~ChildProcess() { close_handles(); }

private:
    void close_handles();

    unsigned long long pid_ = 0;
    std::string error_;
#if defined(_WIN32)
    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
#endif
};

#if defined(_WIN32)

namespace {
std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int len =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

// Build a single command line, quoting arguments that contain spaces. Real code
// should follow the full CommandLineToArgvW quoting rules; this suffices for the
// example's fixed arguments.
std::wstring build_command_line(const std::vector<std::string>& argv) {
    std::wstring command;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) {
            command += L' ';
        }
        const std::wstring arg = utf8_to_wide(argv[i]);
        if (arg.find(L' ') != std::wstring::npos) {
            command += L'"';
            command += arg;
            command += L'"';
        } else {
            command += arg;
        }
    }
    return command;
}
} // namespace

bool ChildProcess::start(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        error_ = "empty argv";
        return false;
    }

    std::wstring command_line = build_command_line(argv);
    command_line.push_back(L'\0'); // CreateProcessW may modify the buffer.

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};

    const BOOL ok = CreateProcessW(nullptr,
                                   command_line.data(),
                                   nullptr,
                                   nullptr,
                                   FALSE,
                                   CREATE_NO_WINDOW,
                                   nullptr,
                                   nullptr,
                                   &startup,
                                   &info);
    if (ok == FALSE) {
        error_ = "CreateProcessW failed (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    process_ = info.hProcess;
    thread_ = info.hThread;
    pid_ = info.dwProcessId;
    return true;
}

bool ChildProcess::try_wait(int& exit_code) {
    if (process_ == nullptr) {
        return true;
    }
    if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) {
        return false;
    }
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    exit_code = static_cast<int>(code);
    return true;
}

void ChildProcess::terminate() {
    if (process_ != nullptr) {
        TerminateProcess(process_, 1);
    }
}

void ChildProcess::close_handles() {
    if (thread_ != nullptr) {
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (process_ != nullptr) {
        CloseHandle(process_);
        process_ = nullptr;
    }
}

#else // POSIX

bool ChildProcess::start(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        error_ = "empty argv";
        return false;
    }

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        raw.push_back(const_cast<char*>(arg.c_str()));
    }
    raw.push_back(nullptr);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, raw[0], nullptr, nullptr, raw.data(), environ);
    if (rc != 0) {
        error_ = "posix_spawnp failed (errno " + std::to_string(rc) + ")";
        return false;
    }
    pid_ = static_cast<unsigned long long>(pid);
    return true;
}

bool ChildProcess::try_wait(int& exit_code) {
    if (pid_ == 0) {
        return true;
    }
    int status = 0;
    pid_t result = 0;
    do {
        result = waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    } while (result < 0 && errno == EINTR);

    if (result == 0) {
        return false; // still running
    }
    if (result < 0) {
        // ECHILD (already reaped) or another error: stop polling and surface it
        // rather than reporting a bogus exit code 0.
        error_ = "waitpid failed (errno " + std::to_string(errno) + ")";
        exit_code = -1;
        pid_ = 0;
        return true;
    }
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    pid_ = 0;
    return true;
}

void ChildProcess::terminate() {
    if (pid_ != 0) {
        ::kill(static_cast<pid_t>(pid_), SIGTERM);
    }
}

void ChildProcess::close_handles() {}

#endif

// --- Example UI --------------------------------------------------------------

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

// A short-lived child that stands in for launching an emulator run.
std::vector<std::string> sample_command() {
#if defined(_WIN32)
    return {"cmd.exe", "/c", "ping -n 3 127.0.0.1 >nul"};
#else
    return {"/bin/sh", "-c", "sleep 2"};
#endif
}

struct RunState {
    std::atomic<bool> cancel_requested = false;
    std::atomic<bool> running = false;
    std::thread worker;

    ~RunState() {
        cancel_requested.store(true, std::memory_order_relaxed);
        if (worker.joinable()) {
            worker.join();
        }
    }
};

void start_run(nk::Application& app,
               const std::shared_ptr<RunState>& state,
               const std::shared_ptr<nk::Label>& summary,
               const std::shared_ptr<nk::StatusBar>& status,
               const std::shared_ptr<nk::Button>& start_button) {
    if (state->running.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    if (state->worker.joinable()) {
        state->worker.join();
    }
    state->cancel_requested.store(false, std::memory_order_relaxed);
    summary->set_text("Launching child process...");
    status->set_segments({"Launching", ""});
    start_button->set_label("Running");

    state->worker = std::thread([&app, state, summary, status, start_button] {
        auto process = std::make_shared<ChildProcess>();
        if (!process->start(sample_command())) {
            const std::string message = process->error();
            app.event_loop().post(
                [state, summary, status, start_button, message] {
                    state->running.store(false, std::memory_order_relaxed);
                    summary->set_text("Launch failed: " + message);
                    status->set_segments({"Error", "Ready"});
                    start_button->set_label("Start process");
                },
                "process-launch-error");
            return;
        }

        // Report the PID on the UI thread.
        const auto pid = process->pid();
        app.event_loop().post(
            [summary, status, pid] {
                summary->set_text("Running child process (pid " + std::to_string(pid) + ")");
                status->set_segments({"Running", "pid " + std::to_string(pid)});
            },
            "process-launch-started");

        // Poll for exit while honoring cancellation. This runs on the worker
        // thread, so sleeping here never touches the UI's responsiveness.
        int exit_code = 0;
        while (!process->try_wait(exit_code)) {
            if (state->cancel_requested.load(std::memory_order_relaxed)) {
                process->terminate();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        const bool cancelled = state->cancel_requested.load(std::memory_order_relaxed);
        app.event_loop().post(
            [&app, state, summary, status, start_button, exit_code, cancelled] {
                state->running.store(false, std::memory_order_relaxed);
                if (cancelled) {
                    summary->set_text("Process terminated by user.");
                    status->set_segments({"Cancelled", "Ready"});
                } else {
                    summary->set_text("Process exited with code " + std::to_string(exit_code) +
                                      ".");
                    status->set_segments({"Exited", "code " + std::to_string(exit_code)});
                }
                start_button->set_label("Start process");
                app.quit(0);
            },
            "process-launch-finished");
    });
}

int main(int argc, char** argv) {
    nk::Application app(argc, argv);

    nk::Window window({
        .title = "Process Launch Pattern",
        .width = 520,
        .height = 220,
    });

    auto state = std::make_shared<RunState>();
    auto root = Box::vertical(10.0F);
    auto title = nk::Label::create("External process monitor");
    auto summary = nk::Label::create("Ready.");
    auto start_button = nk::Button::create("Start process");
    auto cancel_button = nk::Button::create("Terminate");
    auto status = nk::StatusBar::create();
    status->set_segments({"Ready", ""});

    auto start_conn = start_button->on_clicked().connect(
        [&] { start_run(app, state, summary, status, start_button); });
    auto cancel_conn = cancel_button->on_clicked().connect(
        [state] { state->cancel_requested.store(true, std::memory_order_relaxed); });
    (void)start_conn;
    (void)cancel_conn;

    root->append(title);
    root->append(summary);
    root->append(start_button);
    root->append(cancel_button);
    root->append(status);

    window.set_child(root);
    window.present();

    app.event_loop().post([&] { start_run(app, state, summary, status, start_button); },
                          "process-launch-auto-start");

    return app.run();
}
