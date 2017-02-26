#include "channel.h"
#include <boost/asio/write.hpp>
#include <log4cxx/logger.h>
#include "utils.h"

using std::vector;
using std::bind;
using std::placeholders::_1;
using std::placeholders::_2;

using boost::asio::io_service;
using boost::asio::ip::tcp;

using boost::system::error_code;

using log4cxx::Logger;
using log4cxx::LoggerPtr;

namespace roberto {

static const LoggerPtr logger = Logger::getLogger("r.channel");

Channel::Channel(io_service& io_service, tcp::resolver& resolver, const tcp::endpoint& endpoint,
                 StatusCallback status_callback)
: socket_(io_service), resolver_(resolver), endpoint_(endpoint),
  status_callback_(std::move(status_callback)) {

}

void Channel::start() {
    auto callback = bind(&Channel::handle_resolve, shared_from_this(), _1, _2);
    resolver_.async_resolve(endpoint_, move(callback));
}

void Channel::read(size_t max_size) {
    read_buffer_.resize(max_size);
    auto callback = bind(&Channel::handle_read, shared_from_this(), _1, _2);
    socket_.async_read_some(boost::asio::buffer(read_buffer_), move(callback));
}

void Channel::write(const vector<uint8_t>& buffer) {
    write_buffer_ = buffer;
    auto callback = bind(&Channel::handle_write, shared_from_this(), _1, _2);
    boost::asio::async_write(socket_, boost::asio::buffer(write_buffer_), move(callback));
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
            LOG4CXX_INFO(logger, "Failed to resolve " << endpoint_ << ": " << error.message());
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
                LOG4CXX_INFO(logger, "Failed to connect to " << endpoint_ << ": "
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
            LOG4CXX_DEBUG(logger, "Failed to read from connection to " << endpoint_ << ": "
                          << error.message());
        }
        status_callback_(Error{error, Error::Stage::READ});
        return;
    }
    status_callback_(Read{read_buffer_});
}

void Channel::handle_write(const error_code& error, size_t bytes_read) {
    if (error) {
        if (!utils::is_operation_aborted(error)) {
            LOG4CXX_DEBUG(logger, "Failed to write to connection to " << endpoint_ << ": "
                         << error.message());
        }
        status_callback_(Error{error, Error::Stage::WRITE});
        return;
    }
    status_callback_(Write{});
}

} // roberto