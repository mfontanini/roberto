#include "server.h"
#include <functional>
#include <log4cxx/logger.h>
#include <boost/asio/io_service.hpp>
#include "client_connection.h"
#include "authentication_manager.h"

using std::shared_ptr;
using std::make_shared;
using std::bind;

using boost::asio::ip::tcp;
using boost::asio::io_service;

using boost::system::error_code;
using boost::system::system_error;

using log4cxx::Logger;
using log4cxx::LoggerPtr;

namespace roberto {

static const LoggerPtr logger = Logger::getLogger("r.server");

Server::Server(io_service& io_service, const tcp::endpoint& endpoint,
               shared_ptr<AuthenticationManager> auth_manager)
: io_service_(io_service), resolver_(io_service_), acceptor_(io_service_, endpoint),
  auth_manager_(move(auth_manager)) {

}

void Server::start() {
    LOG4CXX_INFO(logger, "Listening for connections on " << acceptor_.local_endpoint());
    start_accept();
}

void Server::start_accept() {
    using std::placeholders::_1;
    auto connection = make_shared<ClientConnection>(io_service_, resolver_, auth_manager_);
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
        try {
            connection->start();
        }
        catch (const system_error& error) {
            LOG4CXX_DEBUG(logger, "Error while starting connection: " << error.what());
        }
        start_accept();
    }
}

} // roberto
