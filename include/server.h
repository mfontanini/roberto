#pragma once

#include <memory>
#include <boost/asio/ip/tcp.hpp>

namespace boost { namespace asio { class io_service; } }

namespace roberto {

class ClientConnection;

class Server {
public:
    Server(boost::asio::io_service& io_service, const boost::asio::ip::tcp::endpoint& endpoint);

    void start();
private:
    void start_accept();
    void on_accept(std::shared_ptr<ClientConnection> connection,
                   const boost::system::error_code& error);

    boost::asio::io_service& io_service_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

} // roberto
