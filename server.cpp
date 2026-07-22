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

// Structure to track live price and dynamic volume
struct StockInfo {
    std::string timestamp;
    double price;
    int volume;
};

// GLOBAL MATRIX: Shared across the data stream engine and parallel workers
// Ticker -> StockInfo
std::unordered_map<std::string, StockInfo> live_market_prices;
std::mutex market_lock; // Mutex protecting concurrent read/writes
const std::string CSV_FILE = "market_data1.csv";

// Helper to strip hidden whitespace/carriage returns (\r)
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Helper to persist RAM inventory changes back to market_data1.csv
void sync_to_csv() {
    std::ofstream file(CSV_FILE, std::ios::trunc);
    if (!file.is_open()) return;

    file << "Date,Ticker,Price,Volume\n";
    for (const auto& [ticker, info] : live_market_prices) {
        file << info.timestamp << "," << ticker << "," << std::fixed << std::setprecision(2)
             << info.price << "," << info.volume << "\n";
    }
    file.close();
}

// Thread-safe BUY/SELL processor
std::string execute_trade(const std::string& action, const std::string& ticker, int qty) {
    std::lock_guard<std::mutex> lock(market_lock);

    if (live_market_prices.find(ticker) == live_market_prices.end()) {
        return "REJECTED | Asset '" + ticker + "' not found in market matrix.";
    }

    StockInfo& stock = live_market_prices[ticker];

    if (action == "BUY") {
        if (stock.volume >= qty) {
            stock.volume -= qty;
            sync_to_csv(); // Flushes updated dynamic volume to CSV
            return "SUCCESS | Bought " + std::to_string(qty) + " shares of " + ticker +
                   " @ $" + std::to_string(stock.price) + ". Remaining Volume: " + std::to_string(stock.volume);
        } else {
            return "REJECTED | Insufficient volume! Requested: " + std::to_string(qty) +
                   ", Available: " + std::to_string(stock.volume);
        }
    } else if (action == "SELL") {
        stock.volume += qty;
        sync_to_csv(); // Flushes updated dynamic volume to CSV
        return "SUCCESS | Sold " + std::to_string(qty) + " shares of " + ticker +
               " @ $" + std::to_string(stock.price) + ". Updated Volume: " + std::to_string(stock.volume);
    }

    return "REJECTED | Invalid trade action.";
}

// PARALLEL CHAT WORKER ROUTINE
void chatbox_worker_routine(zmq::context_t* context) {
    zmq::socket_t worker(*context, zmq::socket_type::rep);
    worker.connect("inproc://backend");

    while (true) {
        zmq::message_t request;
        auto result = worker.recv(request, zmq::recv_flags::none);
        if (!result) continue;

        std::string client_msg(static_cast<char*>(request.data()), request.size());
        client_msg = trim(client_msg);

        std::cout << "\n🧵 [Thread " << std::this_thread::get_id() << "] Picked up request: " << client_msg << std::endl;

        std::string reply_msg;

        // 1. DYNAMIC TRADE COMMANDS (e.g. BUY:AAPL:10 or SELL:AAPL:5)
        if (client_msg.rfind("BUY:", 0) == 0 || client_msg.rfind("SELL:", 0) == 0) {
            std::stringstream ss(client_msg);
            std::string action, ticker, qty_str;
            std::getline(ss, action, ':');
            std::getline(ss, ticker, ':');
            std::getline(ss, qty_str, ':');

            try {
                int qty = std::stoi(qty_str);
                reply_msg = execute_trade(action, ticker, qty);
            } catch (...) {
                reply_msg = "REJECTED | Invalid trade quantity.";
            }
        }
        // 2. FETCH PRICE QUERY (e.g. FETCH:AAPL)
        else if (client_msg.rfind("FETCH:", 0) == 0) {
            std::string target_ticker = client_msg.substr(6);
            target_ticker = trim(target_ticker);

            std::lock_guard<std::mutex> lock(market_lock);
            if (live_market_prices.count(target_ticker)) {
                auto& info = live_market_prices[target_ticker];
                reply_msg = "SUCCESS | Asset: " + target_ticker + " | Price: $" + std::to_string(info.price) +
                            " | Vol: " + std::to_string(info.volume);
            } else {
                reply_msg = "ERROR | Asset '" + target_ticker + "' is not active in the tracking matrix.";
            }
        }
        // 3. BASELINE CONTROLS
        else if (client_msg == "STATUS_CHECK") {
            reply_msg = "HEALTH: Thread-pool processing framework active and online.";
        } else if (client_msg == "MANUAL_OVERRIDE") {
            reply_msg = "SUCCESS: Emergency command protocols initialized.";
        } else {
            reply_msg = "ACK: Query processed by assigned worker thread.";
        }

        // Send reply back to Python client
        zmq::message_t reply(reply_msg.size());
        memcpy(reply.data(), reply_msg.data(), reply_msg.size());
        worker.send(reply, zmq::send_flags::none);

        std::cout << "✅ [Thread " << std::this_thread::get_id() << "] Response sent: " << reply_msg << std::endl;
    }
}

// ROUTER-DEALER TRAFFIC MANAGER
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

    std::cout << "[INIT] Multithreaded Control Plane active with 10 workers on port 5556." << std::endl;

    zmq::proxy(frontend, backend);

    for (auto& t : worker_threads) {
        if (t.joinable()) t.join();
    }
}

// MAIN EXECUTION THREAD (DATA PLANE ENGINE & CONTINUOUS BROADCASTER)
int main() {
    // 1. Pre-load CSV data into memory
    std::ifstream file(CSV_FILE);
    if (!file.is_open()) {
        std::cerr << "CRITICAL ERROR: Could not open " << CSV_FILE << std::endl;
        return 1;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string timestamp, ticker, price_str, volume_str = "1000";

        std::getline(ss, timestamp, ',');
        std::getline(ss, ticker, ',');
        std::getline(ss, price_str, ',');
        std::getline(ss, volume_str, ','); // Optional 4th volume column

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
    std::cout << "✅ Market data matrix loaded into RAM." << std::endl;

    // 2. Fire up the multithreaded control proxy on Port 5556
    std::thread proxy_worker(run_chatbox_proxy_server);
    proxy_worker.detach();

    // 3. Setup Data Broadcast Socket on Port 5555
    zmq::context_t context(1);
    zmq::socket_t publisher(context, zmq::socket_type::pub);
    publisher.bind("tcp://*:5555");

    std::cout << "[INIT] Exchange Stream engine broadcasting on port 5555...\n" << std::endl;

    // 4. Infinite Live Broadcast Loop
    while (true) {
        {
            std::lock_guard<std::mutex> lock(market_lock);
            for (const auto& [ticker, info] : live_market_prices) {
                // Topic string format: TICKER,TIMESTAMP,PRICE,VOLUME
                std::string message_string = ticker + "," + info.timestamp + ",$" + 
                                            std::to_string(info.price) + ",Vol:" + std::to_string(info.volume);

                zmq::message_t message(message_string.size());
                memcpy(message.data(), message_string.data(), message_string.size());
                publisher.send(message, zmq::send_flags::none);

                std::cout << "Broadcasted Data: " << message_string << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Broadcast loop delay
    }

    return 0;
}