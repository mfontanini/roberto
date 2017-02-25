#include "client_connection.h"
#include <cassert>
#include <unordered_set>
#include <log4cxx/logger.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include "socks_messages.h"

using std::unordered_set;

using boost::asio::io_service;

using boost::system::error_code;

using log4cxx::Logger;
using log4cxx::LoggerPtr;

namespace roberto {

static const LoggerPtr logger = Logger::getLogger("r.client_connection");

const ClientConnection::StateHandlerMap ClientConnection::STATE_HANDLERS = {
    { ClientConnection::METHOD_SELECTION, &ClientConnection::handle_method_selection },
    { ClientConnection::METHOD_SELECTION_LIST, &ClientConnection::handle_method_selection_list },
    { ClientConnection::AWAITING_COMMAND, &ClientConnection::handle_command }
};

static unordered_set<uint8_t> SUPPORTED_VERSIONS = { 4, 5 };

ClientConnection::ClientConnection(io_service& io_service)
: socket_(io_service), read_buffer_(4096) {

}

ClientConnection::SocketType& ClientConnection::get_socket() {
    return socket_;
}

const ClientConnection::SocketType& ClientConnection::get_socket() const {
    return socket_;
}

void ClientConnection::start() {
    schedule_read(sizeof(MethodSelectionRequest));
}

void ClientConnection::schedule_read(size_t bytes_read) {
    using namespace std::placeholders;
    auto callback = bind(&ClientConnection::handle_read, this, _1, _2);
    boost::asio::async_read(socket_, boost::asio::buffer(read_buffer_), move(callback));
}

void ClientConnection::schedule_write() {
    using namespace std::placeholders;
    auto callback = bind(&ClientConnection::handle_write, this, _1, _2);
    boost::asio::async_write(socket_, boost::asio::buffer(write_buffer_), move(callback));
}

void ClientConnection::handle_read(const error_code& error, size_t bytes_read) {
    if (error) {
        LOG4CXX_DEBUG(logger, "Failed reading from socket: " << error.message());
        return;
    }
    auto iter = STATE_HANDLERS.find(state_);
    assert(iter != STATE_HANDLERS.end());
    (this->*iter->second)(bytes_read);
}

void ClientConnection::handle_write(const error_code& error, size_t bytes_read) {

}

void ClientConnection::handle_method_selection(size_t bytes_read) {
    assert(bytes_read == sizeof(MethodSelectionRequest));
    const auto* request = cast_buffer<MethodSelectionRequest>();
    if (SUPPORTED_VERSIONS.count(request->version) == 0) {
        LOG4CXX_DEBUG(logger, "Unsupported socks version " << static_cast<int>(request->version));
        return;
    }
    if (request->method_count == 0) {
        LOG4CXX_DEBUG(logger, "Received method selection request with no methods");
        return;
    }
    // We're now waiting for a list of methods. Change state and read them
    state_ = METHOD_SELECTION_LIST;
    schedule_read(request->method_count);
}

void ClientConnection::handle_method_selection_list(size_t bytes_read) {
    // TODO: expand this so we allow plain user/password authentication
    for (size_t i = 0; i < bytes_read; ++i) {
        if (read_buffer_[i] == static_cast<uint8_t>(SocksAuthentication::NONE)) {
            state_ = AWAITING_COMMAND;
            const auto* request = cast_buffer<MethodSelectionRequest>();
            MethodSelectionResponse response{request->version, read_buffer_[i]};
            set_buffer(response);
            schedule_write();
        }
    }
    LOG4CXX_DEBUG(logger, "Ignoring request as no selected authentication method is supported");
}

void ClientConnection::handle_command(size_t bytes_read) {

}

} // roberto
