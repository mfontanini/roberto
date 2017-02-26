#pragma once

#include <memory>
#include <functional>
#include <vector>
#include <cstdint>
#include <boost/variant.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace boost { namespace asio { class io_service; } }

namespace roberto {

class Channel : public std::enable_shared_from_this<Channel> {
public:
    struct Error {
        enum class Stage {
            DNS,
            CONNECT,
            READ,
            WRITE
        };

        boost::system::error_code error;
        Stage error_stage;
    };

    struct Connected {

    };

    struct Read {
        const std::vector<uint8_t>& buffer;
    };

    struct Write {

    };

    using StatusVariant = boost::variant<Error, Connected, Read, Write>;
    using StatusCallback = std::function<void(const StatusVariant&)>;

    Channel(boost::asio::io_service& io_service, boost::asio::ip::tcp::resolver& resolver,
            const boost::asio::ip::tcp::endpoint& endpoint, StatusCallback status_callback);

    const boost::asio::ip::tcp::endpoint& get_target_endpoint() const;
    boost::asio::ip::tcp::endpoint get_local_endpoint() const;

    void start();
    void read(size_t max_size);
    void write(const std::vector<uint8_t>& buffer);
    template <typename ForwardIterator>
    void write(const ForwardIterator& start, const ForwardIterator& end) {
        write_buffer_.assign(start, end);
        write_output_buffer();
    }
private:
    using Resolver = boost::asio::ip::tcp::resolver;

    void connect(Resolver::iterator iter);
    void handle_resolve(const boost::system::error_code& error, Resolver::iterator iter);
    void handle_connect(const boost::system::error_code& error, Resolver::iterator iter);
    void handle_read(const boost::system::error_code& error, size_t bytes_read);
    void handle_write(const boost::system::error_code& error, size_t bytes_read);

    void write_output_buffer();

    boost::asio::ip::tcp::socket socket_;
    Resolver& resolver_;
    boost::asio::ip::tcp::endpoint endpoint_;
    StatusCallback status_callback_;
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
};

} // roberto
