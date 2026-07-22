// ============================================================================
// server.cpp — TradeVerse Dual-Socket Risk Engine + Trade Execution
// ============================================================================
// Architecture:
//   Port 5555 (PUB)           -> Continuous market data + Greek broadcast
//   Port 5556 (ROUTER/DEALER) -> Command/query/trade control plane (thread pool)
//
// Capabilities:
//   - BUY/SELL trade execution with dynamic volume tracking
//   - CSV persistence (sync RAM changes back to disk)
//   - BSM Greek computation across 10,000+ portfolio positions
//   - GREEKS/PORTFOLIO real-time risk queries
//   - Infinite broadcast loop with live data
// ============================================================================

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <iomanip>
#include <cmath>

#include "greeks.hpp"
#include "portfolio.hpp"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Live market state per ticker (price + tradeable volume)
struct StockInfo {
    std::string timestamp;
    double price;
    int volume;
};

// ============================================================================
// GLOBAL SHARED STATE
// ============================================================================

// Live market prices & volume (updated by trades, read by broadcast + Greeks)
std::unordered_map<std::string, StockInfo> live_market_prices;

// Portfolio and its aggregated Greeks
PortfolioSoA global_portfolio;
std::unordered_map<std::string, PortfolioGreeks> live_greeks;
PortfolioGreeks portfolio_total_greeks;

// Mutex for thread-safe concurrent access
std::mutex market_lock;

// Latency tracking
double last_greek_calc_us = 0.0;

// CSV file path
const std::string CSV_FILE = "data/market_data1.csv";
const std::string PORTFOLIO_FILE = "data/portfolio.csv";

// Flag: whether portfolio is loaded (Greeks only work if true)
bool portfolio_loaded = false;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Strip hidden whitespace/carriage returns
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Persist RAM inventory changes back to market_data1.csv
void sync_to_csv() {
    std::ofstream file(CSV_FILE, std::ios::trunc);
    if (!file.is_open()) return;

    file << "Date,Ticker,Price,Volume\n";
    for (const auto& [ticker, info] : live_market_prices) {
        file << info.timestamp << "," << ticker << ","
             << std::fixed << std::setprecision(2)
             << info.price << "," << info.volume << "\n";
    }
    file.close();
}

// Format PortfolioGreeks as a readable string
std::string format_greeks(const PortfolioGreeks& g) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "Delta=" << g.net_delta
        << " | Gamma=" << g.net_gamma
        << " | Vega=" << g.net_vega
        << " | Theta=" << g.net_theta
        << " | Rho=" << g.net_rho
        << " | Notional=" << std::setprecision(2) << g.total_notional
        << " | Positions=" << g.position_count;
    return oss.str();
}

// Recompute Greeks for a ticker after a price change or trade
void refresh_greeks_for_ticker(const std::string& ticker, double spot_price) {
    if (!portfolio_loaded) return;

    auto t_start = std::chrono::high_resolution_clock::now();

    recompute_greeks_for_ticker(global_portfolio, ticker, spot_price);
    live_greeks[ticker] = aggregate_greeks(global_portfolio, ticker);
    portfolio_total_greeks = aggregate_greeks(global_portfolio);

    auto t_end = std::chrono::high_resolution_clock::now();
    last_greek_calc_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
}

// ============================================================================
// TRADE EXECUTION ENGINE
// ============================================================================

// Thread-safe BUY/SELL processor with Greek refresh
std::string execute_trade(const std::string& action, const std::string& ticker, int qty) {
    std::lock_guard<std::mutex> lock(market_lock);

    if (live_market_prices.find(ticker) == live_market_prices.end()) {
        return "REJECTED | Asset '" + ticker + "' not found in market matrix.";
    }

    StockInfo& stock = live_market_prices[ticker];

    if (action == "BUY") {
        if (stock.volume >= qty) {
            stock.volume -= qty;
            sync_to_csv();

            // Refresh Greeks after trade
            refresh_greeks_for_ticker(ticker, stock.price);

            std::ostringstream oss;
            oss << "SUCCESS | Bought " << qty << " shares of " << ticker
                << " @ $" << std::fixed << std::setprecision(2) << stock.price
                << " | Remaining Vol: " << stock.volume;
            if (portfolio_loaded) {
                oss << " | Portfolio Delta: " << std::setprecision(4)
                    << portfolio_total_greeks.net_delta;
            }
            return oss.str();
        } else {
            return "REJECTED | Insufficient volume! Requested: " + std::to_string(qty) +
                   ", Available: " + std::to_string(stock.volume);
        }
    } else if (action == "SELL") {
        stock.volume += qty;
        sync_to_csv();

        // Refresh Greeks after trade
        refresh_greeks_for_ticker(ticker, stock.price);

        std::ostringstream oss;
        oss << "SUCCESS | Sold " << qty << " shares of " << ticker
            << " @ $" << std::fixed << std::setprecision(2) << stock.price
            << " | Updated Vol: " << stock.volume;
        if (portfolio_loaded) {
            oss << " | Portfolio Delta: " << std::setprecision(4)
                << portfolio_total_greeks.net_delta;
        }
        return oss.str();
    }

    return "REJECTED | Invalid trade action.";
}

// ============================================================================
// PARALLEL CHAT WORKER ROUTINE (Control Plane)
// ============================================================================
void chatbox_worker_routine(zmq::context_t* context) {
    zmq::socket_t worker(*context, zmq::socket_type::rep);
    worker.connect("inproc://backend");

    while (true) {
        zmq::message_t request;
        auto result = worker.recv(request, zmq::recv_flags::none);
        if (!result) continue;

        std::string client_msg(static_cast<char*>(request.data()), request.size());
        client_msg = trim(client_msg);

        std::cout << "\n[Thread " << std::this_thread::get_id()
                  << "] Request: " << client_msg << std::endl;

        std::string reply_msg;

        // ------------------------------------------------------------------
        // BUY:<TICKER>:<QTY> or SELL:<TICKER>:<QTY>
        // ------------------------------------------------------------------
        if (client_msg.rfind("BUY:", 0) == 0 || client_msg.rfind("SELL:", 0) == 0) {
            std::stringstream ss(client_msg);
            std::string action, ticker, qty_str;
            std::getline(ss, action, ':');
            std::getline(ss, ticker, ':');
            std::getline(ss, qty_str, ':');

            try {
                int qty = std::stoi(trim(qty_str));
                reply_msg = execute_trade(action, trim(ticker), qty);
            } catch (...) {
                reply_msg = "REJECTED | Invalid trade quantity.";
            }
        }
        // ------------------------------------------------------------------
        // FETCH:<TICKER> — Look up latest price + volume
        // ------------------------------------------------------------------
        else if (client_msg.rfind("FETCH:", 0) == 0) {
            std::string target_ticker = trim(client_msg.substr(6));

            std::lock_guard<std::mutex> lock(market_lock);
            auto it = live_market_prices.find(target_ticker);
            if (it != live_market_prices.end()) {
                auto& info = it->second;
                std::ostringstream oss;
                oss << "SUCCESS | Asset: " << target_ticker
                    << " | Price: $" << std::fixed << std::setprecision(2) << info.price
                    << " | Volume: " << info.volume;
                reply_msg = oss.str();
            } else {
                reply_msg = "ERROR | Asset '" + target_ticker + "' not in tracking matrix.";
            }
        }
        // ------------------------------------------------------------------
        // GREEKS:<TICKER> — Query live Greeks for a specific ticker
        // ------------------------------------------------------------------
        else if (client_msg.rfind("GREEKS:", 0) == 0) {
            std::string target_ticker = trim(client_msg.substr(7));

            std::lock_guard<std::mutex> lock(market_lock);
            if (!portfolio_loaded) {
                reply_msg = "ERROR | Portfolio not loaded. Greeks unavailable.";
            } else {
                auto it = live_greeks.find(target_ticker);
                if (it != live_greeks.end()) {
                    reply_msg = "GREEKS | " + target_ticker + " | " + format_greeks(it->second);
                } else {
                    reply_msg = "ERROR | No Greek data for '" + target_ticker + "'. Send a FETCH first.";
                }
            }
        }
        // ------------------------------------------------------------------
        // PORTFOLIO — Aggregate portfolio-level Greeks + latency
        // ------------------------------------------------------------------
        else if (client_msg == "PORTFOLIO") {
            std::lock_guard<std::mutex> lock(market_lock);
            if (!portfolio_loaded) {
                reply_msg = "ERROR | Portfolio not loaded. Run generate_portfolio.py first.";
            } else {
                reply_msg = "PORTFOLIO | " + format_greeks(portfolio_total_greeks);
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1);
                oss << " | Last Calc: " << last_greek_calc_us << "us";
                reply_msg += oss.str();
            }
        }
        // ------------------------------------------------------------------
        // STATUS_CHECK — Health check
        // ------------------------------------------------------------------
        else if (client_msg == "STATUS_CHECK") {
            std::lock_guard<std::mutex> lock(market_lock);
            std::ostringstream oss;
            oss << "HEALTH: Engine online | Market tickers: " << live_market_prices.size();
            if (portfolio_loaded) {
                oss << " | Portfolio: " << global_portfolio.size() << " positions"
                    << " | Last Greek calc: " << std::fixed << std::setprecision(1)
                    << last_greek_calc_us << "us";
            } else {
                oss << " | Portfolio: NOT LOADED";
            }
            reply_msg = oss.str();
        }
        // ------------------------------------------------------------------
        // MANUAL_OVERRIDE
        // ------------------------------------------------------------------
        else if (client_msg == "MANUAL_OVERRIDE") {
            reply_msg = "SUCCESS: Emergency command protocols initialized.";
        }
        // ------------------------------------------------------------------
        // Default
        // ------------------------------------------------------------------
        else {
            reply_msg = "ACK: Query processed by assigned worker thread.";
        }

        zmq::message_t reply(reply_msg.size());
        memcpy(reply.data(), reply_msg.data(), reply_msg.size());
        worker.send(reply, zmq::send_flags::none);

        std::cout << "[Thread " << std::this_thread::get_id()
                  << "] Response: " << reply_msg << std::endl;
    }
}

// ============================================================================
// ROUTER-DEALER TRAFFIC MANAGER (Control Plane Proxy)
// ============================================================================
void run_chatbox_proxy_server() {
    zmq::context_t context(1);

    zmq::socket_t frontend(context, zmq::socket_type::router);
    frontend.bind("tcp://*:5556");

    zmq::socket_t backend(context, zmq::socket_type::dealer);
    backend.bind("inproc://backend");

    std::vector<std::thread> worker_threads;
    for (int i = 0; i < 10; ++i) {
        worker_threads.push_back(std::thread(chatbox_worker_routine, &context));
    }

    std::cout << "[INIT] Control Plane active with 10 workers on port 5556." << std::endl;

    zmq::proxy(frontend, backend);

    for (auto& t : worker_threads) {
        if (t.joinable()) t.join();
    }
}

// ============================================================================
// MAIN EXECUTION THREAD (Data Plane Engine + Continuous Broadcaster)
// ============================================================================
int main() {
    std::cout << "\n"
              << "+--------------------------------------------------------------+\n"
              << "|   TradeVerse -- Quantitative Risk Engine + Trade Execution    |\n"
              << "|   BSM Greeks + BUY/SELL + Dual-Socket ZMQ Architecture       |\n"
              << "+--------------------------------------------------------------+\n"
              << std::endl;

    // ---- 1. Pre-load market data CSV into RAM ----
    std::cout << "[INIT] Loading market data from " << CSV_FILE << "..." << std::endl;
    {
        std::ifstream file(CSV_FILE);
        if (!file.is_open()) {
            std::cerr << "CRITICAL: Could not open " << CSV_FILE << std::endl;
            return 1;
        }

        std::string line;
        std::getline(file, line); // Skip header: Date,Ticker,Price,Volume

        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string timestamp, ticker, price_str, volume_str;

            std::getline(ss, timestamp, ',');
            std::getline(ss, ticker, ',');
            std::getline(ss, price_str, ',');
            std::getline(ss, volume_str, ',');

            timestamp = trim(timestamp);
            ticker = trim(ticker);
            price_str = trim(price_str);
            volume_str = trim(volume_str);

            try {
                double price = std::stod(price_str);
                int vol = volume_str.empty() ? 1000 : std::stoi(volume_str);
                live_market_prices[ticker] = {timestamp, price, vol};
            } catch (...) {
                continue;
            }
        }
        file.close();
    }

    std::cout << "[INIT] Market data loaded: " << live_market_prices.size() << " tickers [";
    for (const auto& [ticker, info] : live_market_prices) {
        std::cout << " " << ticker << ":$" << std::fixed << std::setprecision(2)
                  << info.price << "(v" << info.volume << ")";
    }
    std::cout << " ]" << std::endl;

    // ---- 2. Load portfolio for Greek computation (optional) ----
    std::cout << "[INIT] Loading portfolio from " << PORTFOLIO_FILE << "..." << std::endl;
    if (load_portfolio(PORTFOLIO_FILE, global_portfolio)) {
        portfolio_loaded = true;

        // Count unique tickers
        std::unordered_map<std::string, int> ticker_counts;
        for (const auto& t : global_portfolio.tickers) ticker_counts[t]++;
        std::cout << "[INIT] Portfolio: " << global_portfolio.size()
                  << " positions across " << ticker_counts.size() << " tickers [";
        for (const auto& [ticker, count] : ticker_counts) {
            std::cout << " " << ticker << ":" << count;
        }
        std::cout << " ]" << std::endl;

        // Initial Greek computation for all tickers with known prices
        {
            std::lock_guard<std::mutex> lock(market_lock);
            for (const auto& [ticker, info] : live_market_prices) {
                refresh_greeks_for_ticker(ticker, info.price);
            }
        }
        std::cout << "[INIT] Initial Greeks computed for all active tickers." << std::endl;
    } else {
        std::cout << "[WARN] Portfolio not loaded. Trade execution works, but Greeks are disabled." << std::endl;
        std::cout << "[WARN] Run: python scripts/generate_portfolio.py to create portfolio.csv" << std::endl;
    }

    // ---- 3. Fire up the multithreaded control proxy ----
    std::thread proxy_worker(run_chatbox_proxy_server);
    proxy_worker.detach();

    // ---- 4. Setup data broadcast (PUB) ----
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind("tcp://*:5555");

    std::cout << "[INIT] Data broadcast engine active on port 5555." << std::endl;
    std::cout << "[INIT] Commands on port 5556: FETCH: | GREEKS: | PORTFOLIO | BUY: | SELL: | STATUS_CHECK" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    // ---- 5. Infinite live broadcast loop ----
    int broadcast_cycle = 0;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(market_lock);
            for (const auto& [ticker, info] : live_market_prices) {
                // Broadcast market data: TICKER,TIMESTAMP,PRICE,VOLUME
                std::ostringstream data_oss;
                data_oss << ticker << "," << info.timestamp << ","
                         << std::fixed << std::setprecision(2)
                         << info.price << "," << info.volume;

                std::string data_msg = data_oss.str();
                zmq::message_t message(data_msg.size());
                memcpy(message.data(), data_msg.data(), data_msg.size());
                publisher.send(message, zmq::send_flags::none);

                // Broadcast Greeks if portfolio is loaded
                if (portfolio_loaded) {
                    auto git = live_greeks.find(ticker);
                    if (git != live_greeks.end()) {
                        const auto& g = git->second;
                        std::ostringstream greek_oss;
                        greek_oss << std::fixed << std::setprecision(6);
                        greek_oss << "GREEKS," << ticker
                                  << "," << g.net_delta
                                  << "," << g.net_gamma
                                  << "," << g.net_vega
                                  << "," << g.net_theta
                                  << "," << g.net_rho;

                        std::string greek_msg = greek_oss.str();
                        zmq::message_t gmsg(greek_msg.size());
                        memcpy(gmsg.data(), greek_msg.data(), greek_msg.size());
                        publisher.send(gmsg, zmq::send_flags::none);
                    }
                }
            }
        }

        // Print summary every 10 cycles
        ++broadcast_cycle;
        if (broadcast_cycle % 10 == 0) {
            std::lock_guard<std::mutex> lock(market_lock);
            std::cout << "[Cycle " << broadcast_cycle << "] Broadcasting "
                      << live_market_prices.size() << " tickers";
            if (portfolio_loaded) {
                std::cout << " | Portfolio Delta: " << std::fixed << std::setprecision(2)
                          << portfolio_total_greeks.net_delta;
            }
            std::cout << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}