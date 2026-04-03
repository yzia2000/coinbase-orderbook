#include "tui_renderer.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace cob {

TuiRenderer::TuiRenderer(std::filesystem::path output_dir,
                           std::chrono::milliseconds refresh_interval)
    : output_dir_(std::move(output_dir))
    , refresh_interval_(refresh_interval)
{
    std::filesystem::create_directories(output_dir_);
}

TuiRenderer::~TuiRenderer()
{
    stop();
}

void TuiRenderer::add_book(std::shared_ptr<Orderbook> book)
{
    books_.push_back(std::move(book));
}

void TuiRenderer::start()
{
    running_ = true;
    render_thread_ = std::thread(&TuiRenderer::render_loop, this);
}

void TuiRenderer::stop()
{
    running_ = false;
    if (render_thread_.joinable()) {
        render_thread_.join();
    }
}

void TuiRenderer::render_loop()
{
    while (running_) {
        for (const auto& book : books_) {
            auto snap = book->load_snapshot();
            auto content = render_book(book->product_id(), snap);

            // Atomic write: write to tmp, then rename
            auto path = output_dir_ / (book->product_id() + ".txt");
            auto tmp_path = output_dir_ / (book->product_id() + ".txt.tmp");

            {
                std::ofstream ofs(tmp_path, std::ios::trunc);
                ofs << content;
            }
            std::filesystem::rename(tmp_path, path);
        }
        std::this_thread::sleep_for(refresh_interval_);
    }
}

std::string TuiRenderer::render_book(const std::string& product_id,
                                      const FlatSnapshot& snap) const
{
    std::ostringstream ss;

    constexpr int price_w = 14;
    constexpr int size_w = 16;
    constexpr int bar_w = 30;
    constexpr int total_w = price_w + size_w + bar_w + 7;

    // Header
    ss << "\033[1;36m" << std::string(total_w, '=') << "\033[0m\n";
    ss << "\033[1;37m  " << product_id << " ORDERBOOK";
    ss << "  (updates: " << snap.update_count << ")";
    ss << "\033[0m\n";
    ss << "\033[1;36m" << std::string(total_w, '=') << "\033[0m\n\n";

    // Find max size for bar scaling
    double max_size = 0.0;
    for (uint32_t i = 0; i < snap.asks.count; ++i)
        max_size = std::max(max_size, snap.asks.levels[i].size);
    for (uint32_t i = 0; i < snap.bids.count; ++i)
        max_size = std::max(max_size, snap.bids.levels[i].size);
    if (max_size == 0.0) max_size = 1.0;

    // Column headers
    ss << "\033[1;37m"
       << std::setw(price_w) << std::right << "PRICE"
       << "  "
       << std::setw(size_w) << std::left << "SIZE"
       << "  "
       << "DEPTH"
       << "\033[0m\n";
    ss << std::string(total_w, '-') << "\n";

    // Asks (reversed so lowest ask is nearest to spread)
    for (int i = static_cast<int>(snap.asks.count) - 1; i >= 0; --i) {
        const auto& lvl = snap.asks.levels[i];
        ss << "\033[31m"
           << std::setw(price_w) << std::right << format_price(lvl.price)
           << "\033[0m"
           << "  "
           << "\033[31m"
           << std::setw(size_w) << std::left << format_size(lvl.size)
           << "\033[0m"
           << "  "
           << "\033[31m" << bar(lvl.size, max_size, bar_w) << "\033[0m"
           << "\n";
    }

    // Spread
    if (snap.bids.count > 0 && snap.asks.count > 0) {
        double best_bid = snap.bids.levels[0].price;
        double best_ask = snap.asks.levels[0].price;
        double spread = best_ask - best_bid;
        double spread_pct = (spread / best_ask) * 100.0;
        ss << "\033[1;33m"
           << std::string(price_w, ' ')
           << "  SPREAD: " << format_price(spread)
           << " (" << std::fixed << std::setprecision(4) << spread_pct << "%)"
           << "\033[0m\n";
    }

    // Bids
    for (uint32_t i = 0; i < snap.bids.count; ++i) {
        const auto& lvl = snap.bids.levels[i];
        ss << "\033[32m"
           << std::setw(price_w) << std::right << format_price(lvl.price)
           << "\033[0m"
           << "  "
           << "\033[32m"
           << std::setw(size_w) << std::left << format_size(lvl.size)
           << "\033[0m"
           << "  "
           << "\033[32m" << bar(lvl.size, max_size, bar_w) << "\033[0m"
           << "\n";
    }

    ss << "\n\033[1;36m" << std::string(total_w, '=') << "\033[0m\n";

    return ss.str();
}

std::string TuiRenderer::format_price(double price) const
{
    std::ostringstream ss;
    if (price >= 1.0) {
        ss << std::fixed << std::setprecision(2) << price;
    } else {
        ss << std::fixed << std::setprecision(6) << price;
    }
    return ss.str();
}

std::string TuiRenderer::format_size(double size) const
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(8) << size;
    return ss.str();
}

std::string TuiRenderer::bar(double size, double max_size, int width) const
{
    int filled = static_cast<int>(std::round((size / max_size) * width));
    filled = std::clamp(filled, 0, width);
    return std::string(filled, '#') + std::string(width - filled, ' ');
}

} // namespace cob
