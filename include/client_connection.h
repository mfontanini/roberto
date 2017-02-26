#pragma once

#include <map>
#include <vector>
#include <type_traits>
#include <cstring>
#include <boost/variant.hpp>
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
    enum ReadState {
        METHOD_SELECTION,
        METHOD_SELECTION_LIST,
        AWAITING_COMMAND,
        AWAITING_COMMAND_ENDPOINT_IPV4,
        AWAITING_COMMAND_ENDPOINT_IPV6,
    };

    enum WriteState {
        SENDING_METHOD
    };

    template <typename T>
    using StateHandlerMap = std::map<T, void (ClientConnection::*)(size_t)>;
    using ReadStateHandlerMap = StateHandlerMap<ReadState>;
    using WriteStateHandlerMap = StateHandlerMap<WriteState>;

    static const ReadStateHandlerMap READ_STATE_HANDLERS;
    static const WriteStateHandlerMap WRITE_STATE_HANDLERS;

    void schedule_read(size_t byte_count, size_t write_offset = 0);
    void schedule_write();

    template <typename T>
    void set_buffer(const T& contents) {
        static_assert(std::is_pod<T>::value, "Only PODs can be written to buffer");
        write_buffer_.resize(sizeof(contents));
        std::memcpy(write_buffer_.data(), &contents, sizeof(contents));
    }

    template <typename T>
    const T* cast_buffer(size_t offset = 0) {
        assert(read_buffer_.size() >= (sizeof(T) + offset));
        return reinterpret_cast<const T*>(read_buffer_.data() + offset);
    }  

    void handle_read(const boost::system::error_code& error, size_t bytes_read);
    void handle_write(const boost::system::error_code& error, size_t bytes_written);

    // Read state handlers
    void handle_method_selection(size_t bytes_read);
    void handle_method_selection_list(size_t bytes_read);
    void handle_command(size_t bytes_read);
    void handle_endpoint_ipv4(size_t bytes_read);
    void handle_endpoint_ipv6(size_t bytes_read);
    void handle_command_endpoint(const boost::asio::ip::tcp::endpoint& endpoint);

    // Write state handlers
    void handle_method_sent(size_t bytes_written);

    boost::asio::ip::tcp::socket socket_;
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
    ReadState read_state_{ReadState::METHOD_SELECTION};
    WriteState write_state_{};
};

} // roberto
