#include "websocket_client.h"
#include <nlohmann/json.hpp>
#include <iostream>

namespace cob {

WebSocketClient::WebSocketClient(net::io_context& ioc, ssl::context& ctx)
    : resolver_(net::make_strand(ioc))
    , ws_(net::make_strand(ioc), ctx)
{
}

void WebSocketClient::connect(const std::string& host, const std::string& port,
                               const std::vector<std::string>& products,
                               MessageCallback on_message)
{
    host_ = host;
    products_ = products;
    on_message_ = std::move(on_message);

    resolver_.async_resolve(host, port,
        beast::bind_front_handler(&WebSocketClient::on_resolve, shared_from_this()));
}

void WebSocketClient::close()
{
    net::post(ws_.get_executor(), [self = shared_from_this()]() {
        beast::error_code ec;
        self->ws_.close(websocket::close_code::normal, ec);
    });
}

void WebSocketClient::on_resolve(beast::error_code ec,
                                  net::ip::tcp::resolver::results_type results)
{
    if (ec) {
        std::cerr << "resolve: " << ec.message() << "\n";
        return;
    }
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
    beast::get_lowest_layer(ws_).async_connect(results,
        beast::bind_front_handler(&WebSocketClient::on_connect, shared_from_this()));
}

void WebSocketClient::on_connect(beast::error_code ec,
                                  net::ip::tcp::resolver::results_type::endpoint_type)
{
    if (ec) {
        std::cerr << "connect: " << ec.message() << "\n";
        return;
    }

    // SNI hostname
    if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str())) {
        ec = beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
        std::cerr << "ssl hostname: " << ec.message() << "\n";
        return;
    }

    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
    ws_.next_layer().async_handshake(ssl::stream_base::client,
        beast::bind_front_handler(&WebSocketClient::on_ssl_handshake, shared_from_this()));
}

void WebSocketClient::on_ssl_handshake(beast::error_code ec)
{
    if (ec) {
        std::cerr << "ssl handshake: " << ec.message() << "\n";
        return;
    }

    beast::get_lowest_layer(ws_).expires_never();

    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(boost::beast::http::field::user_agent, "coinbase-orderbook/0.1");
        }));

    ws_.async_handshake(host_, "/",
        beast::bind_front_handler(&WebSocketClient::on_handshake, shared_from_this()));
}

void WebSocketClient::on_handshake(beast::error_code ec)
{
    if (ec) {
        std::cerr << "ws handshake: " << ec.message() << "\n";
        return;
    }

    std::cout << "Connected to Coinbase WebSocket feed\n";
    send_subscribe(products_);
}

void WebSocketClient::send_subscribe(const std::vector<std::string>& products)
{
    nlohmann::json msg = {
        {"type", "subscribe"},
        {"product_ids", products},
        {"channels", nlohmann::json::array({"level2_batch", "heartbeat"})}
    };

    auto msg_str = std::make_shared<std::string>(msg.dump());
    ws_.async_write(net::buffer(*msg_str),
        [self = shared_from_this(), msg_str](beast::error_code ec, std::size_t bytes) {
            self->on_write(ec, bytes);
        });
}

void WebSocketClient::on_write(beast::error_code ec, std::size_t)
{
    if (ec) {
        std::cerr << "write: " << ec.message() << "\n";
        return;
    }
    do_read();
}

void WebSocketClient::do_read()
{
    ws_.async_read(buffer_,
        beast::bind_front_handler(&WebSocketClient::on_read, shared_from_this()));
}

void WebSocketClient::on_read(beast::error_code ec, std::size_t)
{
    if (ec) {
        if (ec != websocket::error::closed)
            std::cerr << "read: " << ec.message() << "\n";
        return;
    }

    if (on_message_) {
        on_message_(beast::buffers_to_string(buffer_.data()));
    }
    buffer_.consume(buffer_.size());
    do_read();
}

} // namespace cob
