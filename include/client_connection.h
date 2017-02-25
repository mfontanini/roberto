#pragma once

#include <map>
#include <vector>
#include <type_traits>
#include <cstring>
#include <boost/asio/ip/tcp.hpp>

namespace boost { namespace asio { class io_service; } }

namespace roberto {

class ClientConnection {
public:
    using SocketType = boost::asio::ip::tcp::socket;

    ClientConnection(boost::asio::io_service& io_service);

    SocketType& get_socket();
    const SocketType& get_socket() const;

    void start();
private:
    enum State {
        METHOD_SELECTION,
        METHOD_SELECTION_LIST,
        AWAITING_COMMAND,
    };

    using StateHandlerMap = std::map<State, void (ClientConnection::*)(size_t)>;

    static const StateHandlerMap STATE_HANDLERS;

    void schedule_read(size_t bytes_read);
    void schedule_write();

    template <typename T>
    void set_buffer(const T& contents) {
        static_assert(std::is_pod<T>::value, "Only PODs can be written to buffer");
        write_buffer_.resize(sizeof(contents));
        std::memcpy(write_buffer_.data(), &contents, sizeof(contents));
    }

    template <typename T>
    const T* cast_buffer() {
        assert(read_buffer_.size() >= sizeof(T));
        return reinterpret_cast<const T*>(read_buffer_.data());
    }  

    void handle_read(const boost::system::error_code& error, size_t bytes_read);
    void handle_write(const boost::system::error_code& error, size_t bytes_read);

    void handle_method_selection(size_t bytes_read);
    void handle_method_selection_list(size_t bytes_read);
    void handle_command(size_t bytes_read);

    boost::asio::ip::tcp::socket socket_;
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
    State state_{State::METHOD_SELECTION};
};

} // roberto
