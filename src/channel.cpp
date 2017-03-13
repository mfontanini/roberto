#include "channel.h"
#include <sstream>
#include <boost/asio/write.hpp>
#include <log4cxx/logger.h>
#include "utils.h"

using std::vector;
using std::bind;
using std::string;
using std::to_string;
using std::ostringstream;
using std::placeholders::_1;
using std::placeholders::_2;

using boost::asio::io_service;
using boost::asio::ip::tcp;

using boost::system::error_code;

using log4cxx::Logger;
using log4cxx::LoggerPtr;

namespace roberto {

static const LoggerPtr logger = Logger::getLogger("r.channel");

Channel::Channel(io_service& io_service, tcp::resolver& resolver, const string& address,
                 uint16_t port, StatusCallback status_callback)
: socket_(io_service), resolver_(resolver), address_(address), port_(port),
  status_callback_(std::move(status_callback)) {

}

string Channel::get_target_endpoint() const {
    ostringstream output;
    output << address_ << ":" << port_;
    return output.str();
}

tcp::endpoint Channel::get_local_endpoint() const {
    return socket_.local_endpoint();
}

void Channel::start() {
    auto callback = bind(&Channel::handle_resolve, shared_from_this(), _1, _2);
    Resolver::query query(address_, to_string(port_));
    resolver_.async_resolve(query, move(callback));
}

void Channel::cancel() {
    if (socket_.is_open()) {
        socket_.cancel();
    }
}

void Channel::read(size_t max_size) {
    LOG4CXX_TRACE(logger, "Reading at most " << max_size << " bytes from connection to "
                  << get_target_endpoint());
    read_buffer_.resize(max_size);
    auto callback = bind(&Channel::handle_read, shared_from_this(), _1, _2);
    socket_.async_read_some(boost::asio::buffer(read_buffer_), move(callback));
}

void Channel::write(const vector<uint8_t>& buffer) {
    write_buffer_ = buffer;
    LOG4CXX_TRACE(logger, "Writing " << buffer.size() << " bytes into connection to "
                  << get_target_endpoint());
    write_output_buffer();
}

void Channel::connect(Resolver::iterator iter) {
    auto next_iter = iter;
    ++next_iter;
    auto callback = bind(&Channel::handle_connect, shared_from_this(), _1, next_iter);
    socket_.async_connect(*iter, move(callback));
}

void Channel::handle_resolve(const error_code& error, Resolver::iterator iter) {
    if (error) {
        if (!utils::is_operation_aborted(error)) {
            LOG4CXX_INFO(logger, "Failed to resolve " << get_target_endpoint() << ": "
                         << error.message());
        }
        status_callback_(Error{error, Error::Stage::DNS});
        return;
    }
    connect(iter);
}

void Channel::handle_connect(const error_code& error, Resolver::iterator iter) {
    if (error) {
        // If we still have endpoints to attempt a connection to, then don't worry error'ing
        if (iter == Resolver::iterator()) {
            if (!utils::is_operation_aborted(error)) {
                LOG4CXX_INFO(logger, "Failed to connect to " << get_target_endpoint() << ": "
                             << error.message());
            }
            status_callback_(Error{error, Error::Stage::CONNECT});
        }
        else {
            connect(iter);
        }
        return;
    }
    status_callback_(Connected{});
}

void Channel::handle_read(const error_code& error, size_t bytes_read) {
    if (error) {
        if (!utils::is_operation_aborted(error)) {
            LOG4CXX_DEBUG(logger, "Failed to read from connection to " << get_target_endpoint()
                          << ": " << error.message());
        }
        status_callback_(Error{error, Error::Stage::READ});
        return;
    }
    LOG4CXX_TRACE(logger, "Received " << bytes_read << " bytes from connection to "
                  << get_target_endpoint());
    status_callback_(Read{read_buffer_.begin(), read_buffer_.begin() + bytes_read});
}

void Channel::handle_write(const error_code& error, size_t bytes_read) {
    if (error) {
        if (!utils::is_operation_aborted(error)) {
            LOG4CXX_DEBUG(logger, "Failed to write to connection to " << get_target_endpoint()
                          << ": " << error.message());
        }
        status_callback_(Error{error, Error::Stage::WRITE});
        return;
    }
    status_callback_(Write{});
}

void Channel::write_output_buffer() {
    auto callback = bind(&Channel::handle_write, shared_from_this(), _1, _2);
    boost::asio::async_write(socket_, boost::asio::buffer(write_buffer_), move(callback));
}

} // roberto
