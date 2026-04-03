#pragma once

#include "orderbook.h"
#include <string>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>

namespace cob {

class TuiRenderer {
public:
    explicit TuiRenderer(std::filesystem::path output_dir,
                         std::chrono::milliseconds refresh_interval = std::chrono::milliseconds(250));
    ~TuiRenderer();

    void add_book(std::shared_ptr<Orderbook> book);
    void start();
    void stop();

private:
    void render_loop();
    std::string render_book(const Orderbook::Snapshot& snap) const;
    std::string format_price(double price) const;
    std::string format_size(double size) const;
    std::string bar(double size, double max_size, int width) const;

    std::filesystem::path output_dir_;
    std::chrono::milliseconds refresh_interval_;
    std::vector<std::shared_ptr<Orderbook>> books_;
    std::atomic<bool> running_{false};
    std::thread render_thread_;
};

} // namespace cob
