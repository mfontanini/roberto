#include "server.h"
#include <functional>
#include <log4cxx/logger.h>
#include <boost/asio/io_service.hpp>
#include "client_connection.h"

using std::shared_ptr;
using std::make_shared;
using std::bind;

using boost::asio::ip::tcp;
using boost::asio::io_service;

using boost::system::error_code;

using log4cxx::Logger;
using log4cxx::LoggerPtr;

namespace roberto {

static const LoggerPtr logger = Logger::getLogger("r.server");

Server::Server(io_service& io_service, const tcp::endpoint& endpoint)
: io_service_(io_service), acceptor_(io_service_, endpoint) {

}

void Server::start_accept() {
    using std::placeholders::_1;
    auto connection = make_shared<ClientConnection>(io_service_);
    auto callback = bind(&Server::on_accept, this, connection, _1);
    acceptor_.async_accept(connection->get_socket(), move(callback));
}

void Server::on_accept(shared_ptr<ClientConnection> connection, const error_code& error) {
    if (error) {
        if (error != boost::asio::error::operation_aborted) {
            LOG4CXX_ERROR(logger, "Error while accepting socket: " << error.message());
        }
    }
    else {
        connection->start();
        start_accept();
    }
}

} // roberto
