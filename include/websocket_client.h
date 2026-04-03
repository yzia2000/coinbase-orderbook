#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <functional>
#include <string>
#include <vector>

namespace cob {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;

using MessageCallback = std::function<void(const std::string&)>;

class WebSocketClient : public std::enable_shared_from_this<WebSocketClient> {
public:
    WebSocketClient(net::io_context& ioc, ssl::context& ctx);

    void connect(const std::string& host, const std::string& port,
                 const std::vector<std::string>& products,
                 MessageCallback on_message);

    void close();

private:
    void on_resolve(beast::error_code ec, net::ip::tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, net::ip::tcp::resolver::results_type::endpoint_type ep);
    void on_ssl_handshake(beast::error_code ec);
    void on_handshake(beast::error_code ec);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void do_read();

    void send_subscribe(const std::vector<std::string>& products);

    net::ip::tcp::resolver resolver_;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    beast::flat_buffer buffer_;
    std::string host_;
    std::vector<std::string> products_;
    MessageCallback on_message_;
};

} // namespace cob
