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

const ClientConnection::ReadStateHandlerMap ClientConnection::READ_STATE_HANDLERS = {
    { ClientConnection::METHOD_SELECTION, &ClientConnection::handle_method_selection },
    { ClientConnection::METHOD_SELECTION_LIST, &ClientConnection::handle_method_selection_list },
    { ClientConnection::AWAITING_COMMAND, &ClientConnection::handle_command }
};

const ClientConnection::WriteStateHandlerMap ClientConnection::WRITE_STATE_HANDLERS = {
    { ClientConnection::SENDING_METHOD, &ClientConnection::handle_method_sent },
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

void ClientConnection::schedule_read(size_t byte_count, size_t write_offset) {
    using namespace std::placeholders;
    if (byte_count + write_offset > read_buffer_.size()) {
        LOG4CXX_ERROR(logger, "Trying to write past buffer when reading on state "
                      << static_cast<int>(read_state_));
    }
    auto callback = bind(&ClientConnection::handle_read, this, _1, _2);
    auto buffer_start = read_buffer_.data() + write_offset;
    boost::asio::async_read(socket_, boost::asio::buffer(buffer_start, byte_count),
                            move(callback));
}

void ClientConnection::schedule_write() {
    using namespace std::placeholders;
    auto callback = bind(&ClientConnection::handle_write, this, _1, _2);
    boost::asio::async_write(socket_, boost::asio::buffer(write_buffer_), move(callback));
}

void ClientConnection::handle_read(const error_code& error, size_t bytes_read) {
    if (error) {
        if (error != boost::asio::error::operation_aborted) {
            LOG4CXX_DEBUG(logger, "Failed while reading from socket: " << error.message());
        }
        return;
    }
    auto iter = READ_STATE_HANDLERS.find(read_state_);
    assert(iter != READ_STATE_HANDLERS.end());
    (this->*iter->second)(bytes_read);
}

void ClientConnection::handle_write(const error_code& error, size_t bytes_written) {
    if (error) {
        if (error != boost::asio::error::operation_aborted) {
            LOG4CXX_DEBUG(logger, "Error while writing to socket: " << error.message());
        }
        return;
    }
    auto iter = WRITE_STATE_HANDLERS.find(write_state_);
    assert(iter != WRITE_STATE_HANDLERS.end());
    (this->*iter->second)(bytes_written);
}

void ClientConnection::handle_method_selection(size_t) {
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
    read_state_ = METHOD_SELECTION_LIST;
    schedule_read(request->method_count);
}

void ClientConnection::handle_method_selection_list(size_t bytes_read) {
    // TODO: expand this so we allow plain user/password authentication
    for (size_t i = 0; i < bytes_read; ++i) {
        if (read_buffer_[i] == static_cast<uint8_t>(SocksAuthentication::NONE)) {
            const auto* request = cast_buffer<MethodSelectionRequest>();

            set_buffer(MethodSelectionResponse{request->version, read_buffer_[i]});
            write_state_ = SENDING_METHOD;
            schedule_write();
        }
    }
    LOG4CXX_DEBUG(logger, "Ignoring request as no selected authentication method is supported");
}

void ClientConnection::handle_command(size_t) {
    const auto* command = cast_buffer<SocksCommandHeader>();
    if (SUPPORTED_VERSIONS.count(command->version) == 0) {
        LOG4CXX_DEBUG(logger, "Unsupported socks version " << static_cast<int>(command->version));
        return;
    }
    size_t next_read_size = 0;
    // TODO add support for domain names
    switch (static_cast<AddressType>(command->address_type)) {
        case AddressType::IPV4:
            read_state_ = AWAITING_COMMAND_ADDRESS_IPV4;
            next_read_size = 4;
            break;
        case AddressType::IPV6:
            read_state_ = AWAITING_COMMAND_ADDRESS_IPV6;
            next_read_size = 16;
            break;
        default:
            LOG4CXX_DEBUG(logger, "Unsupported address type " << (int)command->address_type);
            return;
    }
    // Read the amount of bytes we're supposed to after the end of the current header
    schedule_read(next_read_size, sizeof(SocksCommandHeader));
}

void ClientConnection::handle_method_sent(size_t bytes_written) {
    assert(bytes_written == sizeof(MethodSelectionResponse));
    read_state_ = AWAITING_COMMAND;
    schedule_read(sizeof(SocksCommandHeader));
}

} // roberto
