#pragma once

#include <map>
#include <vector>
#include <type_traits>
#include <cstring>
#include <memory>
#include <boost/variant.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/variant/static_visitor.hpp>
#include "channel.h"

namespace boost { namespace asio { class io_service; } }

namespace roberto {

class ClientConnection : public std::enable_shared_from_this<ClientConnection> {
public:
    using SocketType = boost::asio::ip::tcp::socket;

    ClientConnection(boost::asio::io_service& io_service,
                     boost::asio::ip::tcp::resolver& resolver);

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
        PROXY_READ,
    };

    enum WriteState {
        SENDING_METHOD,
        SENDING_COMMAND_RESPONSE,
        PROXY_WRITE
    };

    template <typename T>
    using StateHandlerMap = std::map<T, void (ClientConnection::*)(size_t)>;
    using ReadStateHandlerMap = StateHandlerMap<ReadState>;
    using WriteStateHandlerMap = StateHandlerMap<WriteState>;

    struct VariantDispatcher : public boost::static_visitor<void> {
        VariantDispatcher(ClientConnection& connection)
        : connection(connection) {

        }

        template <typename T>
        void operator()(const T& concrete_status) {
            connection.handle_channel_status(concrete_status);
        }

        ClientConnection& connection;
    };

    friend class VariantDispatcher;

    static const ReadStateHandlerMap READ_STATE_HANDLERS;
    static const WriteStateHandlerMap WRITE_STATE_HANDLERS;

    void schedule_read(size_t byte_count, size_t write_offset = 0);
    void schedule_read_some();
    void schedule_write();

    template <typename T>
    void set_buffer(const T& contents, size_t offset = 0) {
        static_assert(std::is_pod<T>::value, "Only PODs can be written to buffer");
        write_buffer_.resize(sizeof(contents) + offset);
        std::memcpy(write_buffer_.data() + offset, &contents, sizeof(contents));
    }

    template <typename ForwardIterator>
    void set_buffer(const ForwardIterator& start, const ForwardIterator& end) {
        write_buffer_.assign(start, end);
    }

    template <typename T>
    const T* cast_buffer(size_t offset = 0) {
        assert(read_buffer_.size() >= (sizeof(T) + offset));
        return reinterpret_cast<const T*>(read_buffer_.data() + offset);
    }  

    void handle_read(const boost::system::error_code& error, size_t bytes_read);
    void handle_write(const boost::system::error_code& error, size_t bytes_written);

    // Channel status handlers
    void handle_channel_status_update(const Channel::StatusVariant& status);
    void handle_channel_status(const Channel::Error& status);
    void handle_channel_status(const Channel::Connected& status);
    void handle_channel_status(const Channel::Read& status);
    void handle_channel_status(const Channel::Write& status);

    // Read state handlers
    void handle_method_selection(size_t bytes_read);
    void handle_method_selection_list(size_t bytes_read);
    void handle_command(size_t bytes_read);
    void handle_endpoint_ipv4(size_t bytes_read);
    void handle_endpoint_ipv6(size_t bytes_read);
    void handle_command_endpoint(const boost::asio::ip::tcp::endpoint& endpoint);
    void handle_client_read(size_t bytes_read);

    // Write state handlers
    void handle_method_sent(size_t bytes_written);
    void handle_command_response_sent(size_t bytes_written);
    void handle_client_write(size_t bytes_written);

    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver& resolver_;
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
    std::shared_ptr<Channel> outbound_connection_;
    ReadState read_state_{ReadState::METHOD_SELECTION};
    WriteState write_state_{};
};

} // roberto
