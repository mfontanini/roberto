#include "client_connection.h"
#include <cassert>
#include <unordered_set>
#include <algorithm>
#include <log4cxx/logger.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include "socks_messages.h"
#include "utils.h"

using std::unordered_set;
using std::copy;
using std::make_shared;

using std::placeholders::_1;
using std::placeholders::_2;

using boost::asio::io_service;
using boost::asio::ip::address;
using boost::asio::ip::address_v4;
using boost::asio::ip::address_v6;
using boost::asio::ip::tcp;

using boost::system::error_code;
using boost::system::system_error;

using boost::static_visitor;
using boost::apply_visitor;

using log4cxx::Logger;
using log4cxx::LoggerPtr;

namespace roberto {

static const LoggerPtr logger = Logger::getLogger("r.client_connection");

const ClientConnection::ReadStateHandlerMap ClientConnection::READ_STATE_HANDLERS = {
    { ClientConnection::METHOD_SELECTION, &ClientConnection::handle_method_selection },
    { ClientConnection::METHOD_SELECTION_LIST, &ClientConnection::handle_method_selection_list },
    { ClientConnection::AWAITING_COMMAND, &ClientConnection::handle_command },
    { ClientConnection::AWAITING_COMMAND_ENDPOINT_IPV4, &ClientConnection::handle_endpoint_ipv4 },
    { ClientConnection::AWAITING_COMMAND_ENDPOINT_IPV6, &ClientConnection::handle_endpoint_ipv6 },
    { ClientConnection::PROXY_READ, &ClientConnection::handle_client_read },
};

const ClientConnection::WriteStateHandlerMap ClientConnection::WRITE_STATE_HANDLERS = {
    { ClientConnection::SENDING_METHOD, &ClientConnection::handle_method_sent },
    { ClientConnection::SENDING_COMMAND_RESPONSE, &ClientConnection::handle_command_response_sent },
    { ClientConnection::PROXY_WRITE, &ClientConnection::handle_client_write },
};

static unordered_set<uint8_t> SUPPORTED_VERSIONS = { 4, 5 };

ClientConnection::ClientConnection(io_service& io_service, tcp::resolver& resolver)
: socket_(io_service), resolver_(resolver), read_buffer_(4096) {

}

ClientConnection::SocketType& ClientConnection::get_socket() {
    return socket_;
}

const ClientConnection::SocketType& ClientConnection::get_socket() const {
    return socket_;
}

void ClientConnection::start() {
    endpoint_ = socket_.remote_endpoint();
    LOG4CXX_INFO(logger, "Accepted client connection from " << endpoint_);

    schedule_read(sizeof(MethodSelectionRequest));
}

void ClientConnection::cancel() {
    if (outbound_connection_) {
        outbound_connection_->cancel();
        LOG4CXX_INFO(logger, "Closing connection to "
                     << outbound_connection_->get_target_endpoint());
        outbound_connection_.reset();
    }
    socket_.cancel();
}

void ClientConnection::schedule_read(size_t byte_count, size_t write_offset) {
    if (byte_count + write_offset > read_buffer_.size()) {
        LOG4CXX_ERROR(logger, "Trying to write past buffer when reading on state "
                      << static_cast<int>(read_state_));
        return;
    }
    LOG4CXX_TRACE(logger, "Reading " << byte_count << " bytes from connection for "
                  << endpoint_);
    auto callback = bind(&ClientConnection::handle_read, shared_from_this(), _1, _2);
    auto buffer_start = read_buffer_.data() + write_offset;
    boost::asio::async_read(socket_, boost::asio::buffer(buffer_start, byte_count),
                            move(callback));
}

void ClientConnection::schedule_read_some() {
    auto callback = bind(&ClientConnection::handle_read, shared_from_this(), _1, _2);
    socket_.async_read_some(boost::asio::buffer(read_buffer_), move(callback));
}

void ClientConnection::schedule_write() {
    LOG4CXX_TRACE(logger, "Writing " << write_buffer_.size() << " bytes into connection for "
                  << endpoint_);
    auto callback = bind(&ClientConnection::handle_write, shared_from_this(), _1, _2);
    boost::asio::async_write(socket_, boost::asio::buffer(write_buffer_), move(callback));
}

void ClientConnection::handle_read(const error_code& error, size_t bytes_read) {
    if (error) {
        if (!utils::is_operation_aborted(error)) {
            LOG4CXX_DEBUG(logger, "Failed while reading from socket: " << error.message());
        }
        cancel();
        return;
    }
    auto iter = READ_STATE_HANDLERS.find(read_state_);
    assert(iter != READ_STATE_HANDLERS.end());
    (this->*iter->second)(bytes_read);
}

void ClientConnection::handle_write(const error_code& error, size_t bytes_written) {
    if (error) {
        if (!utils::is_operation_aborted(error)) {
            LOG4CXX_DEBUG(logger, "Error while writing to socket: " << error.message());
        }
        cancel();
        return;
    }
    auto iter = WRITE_STATE_HANDLERS.find(write_state_);
    assert(iter != WRITE_STATE_HANDLERS.end());
    (this->*iter->second)(bytes_written);
}

void ClientConnection::handle_channel_status_update(const Channel::StatusVariant& status) {
    // If we've already destroyed the outbound connection, ignore this
    if (!outbound_connection_) {
        return;
    }
    VariantDispatcher visitor{*this};
    apply_visitor(visitor, status);
}

void ClientConnection::handle_channel_status(const Channel::Error& status) {
    // Upon any errors, destroy our reference to the channel
    cancel();
}

void ClientConnection::handle_channel_status(const Channel::Connected& /*status*/) {
    LOG4CXX_INFO(logger, "Connection to " << outbound_connection_->get_target_endpoint()
                 << " established");
    SocksCommandResponseHeader response;
    response.version = cast_buffer<SocksCommandHeader>()->version;
    response.reply = static_cast<int>(ReplyType::SUCCESS);
    response.reserved = 0;
    tcp::endpoint local_endpoint;
    try {
        local_endpoint = outbound_connection_->get_local_endpoint();
    }
    catch (const system_error& error) {
        LOG4CXX_DEBUG(logger, "Error getting local endpoint: " << error.what());
        response.reply = static_cast<int>(ReplyType::GENERAL_FAILURE);
    }
    if (response.reply == static_cast<int>(ReplyType::SUCCESS)) {
        // Write the local endpoint after the response header
        const uint16_t port = htons(local_endpoint.port());
        if (local_endpoint.address().is_v4()) {
            response.address_type = static_cast<uint8_t>(AddressType::IPV4);
            uint32_t address = local_endpoint.address().to_v4().to_ulong();
            SocksCommandResponseEndpointIPv4 body{address, port};
            set_buffer(body, sizeof(response));
        }
        else if (local_endpoint.address().is_v6()) {
            response.address_type = static_cast<uint8_t>(AddressType::IPV6);
            auto address_bytes = local_endpoint.address().to_v6().to_bytes();
            SocksCommandResponseEndpointIPv6 body;
            copy(address_bytes.begin(), address_bytes.end(), body.bind_ipv6_address);
            body.bind_port = port;
            set_buffer(body, sizeof(response));
        }
        else {
            LOG4CXX_DEBUG(logger, "Unknown address found on local endpoint");
            response.reply = static_cast<int>(ReplyType::GENERAL_FAILURE);
        }
    }
    // Prepend our header
    memcpy(write_buffer_.data(), &response, sizeof(response));
    write_state_ = SENDING_COMMAND_RESPONSE;
    schedule_write();
}

void ClientConnection::handle_channel_status(const Channel::Read& status) {
    set_buffer(status.buffer_start, status.buffer_end);
    schedule_write();
}

void ClientConnection::handle_channel_status(const Channel::Write& /*status*/) {
    // When we finish writing a buffer into the outbound connection, we can read again from
    // our client
    schedule_read_some();
}

void ClientConnection::handle_method_selection(size_t /*bytes_read*/) {
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
    // Read after our request so we can still access the current data
    schedule_read(request->method_count, sizeof(MethodSelectionRequest));
}

void ClientConnection::handle_method_selection_list(size_t bytes_read) {
    // TODO: expand this so we allow plain user/password authentication
    const size_t offset = sizeof(MethodSelectionRequest);
    for (size_t i = 0; i < bytes_read; ++i) {
        const uint8_t method = read_buffer_[offset + i];
        if (method == static_cast<uint8_t>(SocksAuthentication::NONE)) {
            const auto* request = cast_buffer<MethodSelectionRequest>();

            set_buffer(MethodSelectionResponse{request->version, method});
            write_state_ = SENDING_METHOD;
            schedule_write();
            return;
        }
    }
    LOG4CXX_DEBUG(logger, "Ignoring request as no selected authentication method is supported");
}

void ClientConnection::handle_command(size_t /*bytes_read*/) {
    const auto* command = cast_buffer<SocksCommandHeader>();
    if (SUPPORTED_VERSIONS.count(command->version) == 0) {
        LOG4CXX_DEBUG(logger, "Unsupported socks version " << static_cast<int>(command->version));
        return;
    }
    size_t next_read_size = 0;
    // TODO add support for domain names
    switch (static_cast<AddressType>(command->address_type)) {
        case AddressType::IPV4:
            read_state_ = AWAITING_COMMAND_ENDPOINT_IPV4;
            next_read_size = sizeof(SocksCommandEndpointIPv4);
            break;
        case AddressType::IPV6:
            read_state_ = AWAITING_COMMAND_ENDPOINT_IPV6;
            next_read_size = sizeof(SocksCommandEndpointIPv6);
            break;
        default:
            LOG4CXX_DEBUG(logger, "Unsupported address type " << (int)command->address_type);
            return;
    }
    // Read the amount of bytes we're supposed to after the end of the current header
    schedule_read(next_read_size, sizeof(SocksCommandHeader));
}

void ClientConnection::handle_endpoint_ipv4(size_t /*bytes_read*/) {
    // Read the endpoint part of this command (skip header)
    const auto* endpoint = cast_buffer<SocksCommandEndpointIPv4>(sizeof(SocksCommandHeader));
    address endpoint_address = address_v4(ntohl(endpoint->address));

    tcp::endpoint tcp_endpoint(endpoint_address, ntohs(endpoint->port));
    handle_command_endpoint(tcp_endpoint);
}

void ClientConnection::handle_endpoint_ipv6(size_t bytes_read) {
    // Read the endpoint part of this command (skip header)
    const auto* endpoint = cast_buffer<SocksCommandEndpointIPv6>(sizeof(SocksCommandHeader));
    address_v6::bytes_type address_buffer;
    copy(endpoint->address, endpoint->address + 16, address_buffer.begin());
    address endpoint_address = address_v6(address_buffer);
    
    tcp::endpoint tcp_endpoint(endpoint_address, ntohs(endpoint->port));   
    handle_command_endpoint(tcp_endpoint);
}

void ClientConnection::handle_command_endpoint(const tcp::endpoint& endpoint) {
    const auto* command = cast_buffer<SocksCommandHeader>();
    switch (static_cast<CommandType>(command->command)) {
        case CommandType::CONNECT:

            break;
        default:
            LOG4CXX_DEBUG(logger, "Ignoring command request due to unsupported command: "
                          << static_cast<int>(command->command));
            return;
    }
    LOG4CXX_DEBUG(logger, "Received connection request for " << endpoint);
    auto callback = bind(&ClientConnection::handle_channel_status_update, shared_from_this(), _1);
    outbound_connection_ = make_shared<Channel>(socket_.get_io_service(), resolver_, endpoint,
                                                std::move(callback));
    outbound_connection_->start();
}

void ClientConnection::handle_client_read(size_t bytes_read) {
    // Forward the read bytes into our outbound connection
    outbound_connection_->write(read_buffer_.begin(), read_buffer_.begin() + bytes_read);
}

void ClientConnection::handle_method_sent(size_t bytes_written) {
    assert(bytes_written == sizeof(MethodSelectionResponse));
    read_state_ = AWAITING_COMMAND;
    schedule_read(sizeof(SocksCommandHeader));
}

void ClientConnection::handle_command_response_sent(size_t bytes_written) {
    // TODO: add endpoint on this thing
    LOG4CXX_DEBUG(logger, "Starting proxying connection");
    read_state_ = PROXY_READ;
    write_state_ = PROXY_WRITE;
    // Try to read at most our read buffer size from our outbound connection
    outbound_connection_->read(read_buffer_.size());
    // Try to read from out client's connection
    schedule_read_some();
}

void ClientConnection::handle_client_write(size_t /*bytes_written*/) {
    // We finished forwarding data to our client. It's time to read again from the outbound 
    // connection
    outbound_connection_->read(read_buffer_.size());
}

} // roberto
