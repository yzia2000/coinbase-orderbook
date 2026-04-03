#include "websocket_client.h"
#include "orderbook.h"
#include "tui_renderer.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <csignal>
#include <atomic>

using json = nlohmann::json;

static std::atomic<bool> g_running{true};

static void signal_handler(int)
{
    g_running = false;
}

static std::vector<cob::PriceLevel> parse_levels(const json& arr)
{
    std::vector<cob::PriceLevel> levels;
    levels.reserve(arr.size());
    for (const auto& entry : arr) {
        double price = std::stod(entry[0].get<std::string>());
        double size  = std::stod(entry[1].get<std::string>());
        levels.push_back({price, size});
    }
    return levels;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> products;
    std::string output_dir = "output";

    // Parse CLI args: products as positional, --output for dir
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: coinbase-orderbook [--output DIR] PRODUCT [PRODUCT...]\n"
                      << "  e.g. coinbase-orderbook BTC-USD ETH-USD SOL-USD\n"
                      << "\nOutput files are written to DIR (default: output/)\n"
                      << "View with: watch -t -n 0.3 --color cat output/BTC-USD.txt\n";
            return 0;
        } else {
            products.push_back(arg);
        }
    }

    if (products.empty()) {
        products = {"BTC-USD", "ETH-USD", "SOL-USD"};
        std::cout << "No products specified, defaulting to: BTC-USD ETH-USD SOL-USD\n";
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create orderbooks
    std::unordered_map<std::string, std::shared_ptr<cob::Orderbook>> books;
    for (const auto& product : products) {
        books[product] = std::make_shared<cob::Orderbook>(product);
    }

    // Set up TUI renderer
    cob::TuiRenderer renderer(output_dir, std::chrono::milliseconds(250));
    for (const auto& [id, book] : books) {
        renderer.add_book(book);
    }
    renderer.start();

    // WebSocket setup
    boost::asio::io_context ioc;
    boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(boost::asio::ssl::verify_peer);

    auto client = std::make_shared<cob::WebSocketClient>(ioc, ctx);

    client->connect("ws-feed.exchange.coinbase.com", "443", products,
        [&books](const std::string& msg) {
            try {
                auto j = json::parse(msg);
                auto type = j.value("type", "");

                if (type == "snapshot") {
                    auto product_id = j["product_id"].get<std::string>();
                    auto it = books.find(product_id);
                    if (it != books.end()) {
                        auto bids = parse_levels(j["bids"]);
                        auto asks = parse_levels(j["asks"]);
                        it->second->apply_snapshot(bids, asks);
                        std::cout << "Received snapshot for " << product_id
                                  << " (" << bids.size() << " bids, "
                                  << asks.size() << " asks)\n";
                    }
                } else if (type == "l2update") {
                    auto product_id = j["product_id"].get<std::string>();
                    auto it = books.find(product_id);
                    if (it != books.end()) {
                        std::vector<cob::PriceLevel> bids, asks;
                        for (const auto& change : j["changes"]) {
                            auto side  = change[0].get<std::string>();
                            double price = std::stod(change[1].get<std::string>());
                            double size  = std::stod(change[2].get<std::string>());
                            if (side == "buy") {
                                bids.push_back({price, size});
                            } else {
                                asks.push_back({price, size});
                            }
                        }
                        it->second->apply_update(bids, asks);
                    }
                }
                // heartbeat and other types are silently ignored
            } catch (const std::exception& e) {
                std::cerr << "Error processing message: " << e.what() << "\n";
            }
        });

    // Run io_context in a thread
    std::thread io_thread([&ioc]() { ioc.run(); });

    // Wait for signal
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down...\n";
    client->close();
    renderer.stop();
    ioc.stop();
    if (io_thread.joinable()) io_thread.join();

    return 0;
}
