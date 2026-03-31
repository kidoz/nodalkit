#include <nk/foundation/signal.h>

namespace nk {

// --- Connection ---

Connection::Connection(std::shared_ptr<detail::ConnectionState> state)
    : state_(std::move(state)) {}

void Connection::disconnect() {
    if (state_) {
        state_->connected.store(false, std::memory_order_relaxed);
    }
}

bool Connection::connected() const {
    return state_ && state_->connected.load(std::memory_order_relaxed);
}

// --- ScopedConnection ---

ScopedConnection::ScopedConnection(Connection conn)
    : conn_(std::move(conn)) {}

ScopedConnection::~ScopedConnection() { disconnect(); }

ScopedConnection::ScopedConnection(ScopedConnection&& other) noexcept
    : conn_(std::move(other.conn_)) {}

ScopedConnection& ScopedConnection::operator=(ScopedConnection&& other) noexcept {
    if (this != &other) {
        disconnect();
        conn_ = std::move(other.conn_);
    }
    return *this;
}

void ScopedConnection::disconnect() { conn_.disconnect(); }

Connection ScopedConnection::release() {
    auto c = std::move(conn_);
    conn_ = Connection{};
    return c;
}

bool ScopedConnection::connected() const { return conn_.connected(); }

} // namespace nk
