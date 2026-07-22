#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <iomanip>

// Structure to store dynamic stock details
struct StockInfo {
    double price;
    int available_volume; // Dynamic stock pool
};

// Global in-memory market state
// Structure: market_data[Ticker][Date] = StockInfo
std::unordered_map<std::string, std::unordered_map<std::string, StockInfo>> market_data;
std::mutex market_mutex; // Protects inventory from race conditions across threads
const std::string CSV_FILE = "market_data1.csv";

// Helper to load CSV into RAM at startup
void load_market_data() {
    std::ifstream file(CSV_FILE);
    if (!file.is_open()) {
        std::cerr << "❌ Error: Could not open " << CSV_FILE << std::endl;
        return;
    }

    std::string line, date, ticker, price_str, volume_str;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::getline(ss, date, ',');
        std::getline(ss, ticker, ',');
        std::getline(ss, price_str, ',');
        std::getline(ss, volume_str, ',');

        try {
            double price = std::stod(price_str);
            int volume = std::stoi(volume_str);
            market_data[ticker][date] = {price, volume};
        } catch (...) {
            continue; // Skip malformed lines
        }
    }
    file.close();
    std::cout << "✅ Market data successfully loaded into RAM.\n";
}

// Helper to flush RAM changes back to market_data1.csv
void sync_to_csv() {
    std::ofstream file(CSV_FILE, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "❌ Error: Failed to write updates to " << CSV_FILE << std::endl;
        return;
    }

    file << "Date,Ticker,Price,Volume\n";
    for (const auto& [ticker, date_map] : market_data) {
        for (const auto& [date, info] : date_map) {
            file << date << "," << ticker << "," << std::fixed << std::setprecision(2) 
                 << info.price << "," << info.available_volume << "\n";
        }
    }
    file.close();
}

// Thread-safe order execution engine
std::string process_order(const std::string& action, const std::string& ticker, const std::string& date, int qty) {
    // Lock mutex so concurrent terminal threads execute sequentially
    std::lock_guard<std::mutex> lock(market_mutex);

    // 1. Verify Ticker existence
    if (market_data.find(ticker) == market_data.end()) {
        return "REJECTED: Ticker symbol '" + ticker + "' not found in market data.";
    }

    // 2. Verify Date existence for ticker
    if (market_data[ticker].find(date) == market_data[ticker].end()) {
        return "REJECTED: No market data available for ticker " + ticker + " on date " + date + ".";
    }

    StockInfo& stock = market_data[ticker][date];

    // 3. BUY Logic with Inventory Limit Enforcement
    if (action == "BUY") {
        if (stock.available_volume >= qty) {
            stock.available_volume -= qty; // Deduct inventory
            sync_to_csv();                 // Persist change to CSV
            return "SUCCESS: Bought " + std::to_string(qty) + " shares of " + ticker + 
                   " on " + date + " @ $" + std::to_string(stock.price) + 
                   ". Remaining market volume: " + std::to_string(stock.available_volume);
        } else {
            return "REJECTED: Insufficient inventory! Requested: " + std::to_string(qty) + 
                   ", Available: " + std::to_string(stock.available_volume);
        }
    }
    // 4. SELL Logic (Adds back to dynamic pool)
    else if (action == "SELL") {
        stock.available_volume += qty; // Expand market supply
        sync_to_csv();                 // Persist change to CSV
        return "SUCCESS: Sold " + std::to_string(qty) + " shares of " + ticker + 
               " on " + date + " @ $" + std::to_string(stock.price) + 
               ". Updated market volume: " + std::to_string(stock.available_volume);
    }

    return "REJECTED: Invalid action string '" + action + "'. Use BUY or SELL.";
}

int main() {
    // Boot sequence: Load CSV into memory
    load_market_data();

    std::cout << "\n--- TradeVerse Market Order Execution Engine Online ---\n\n";

    // Example Test Commands (Simulating client orders):
    std::cout << process_order("BUY", "AAPL", "2026-07-08", 10) << std::endl;
    std::cout << process_order("BUY", "AAPL", "2026-07-08", 500000) << std::endl; // Exceeds limit
    std::cout << process_order("SELL", "AAPL", "2026-07-08", 5) << std::endl;

    return 0;
}