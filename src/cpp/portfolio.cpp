// ============================================================================
// portfolio.cpp — Portfolio Loader & Risk Aggregation Implementation
// ============================================================================

#include "portfolio.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <algorithm>

// ============================================================================
// Default Market Parameters
// ============================================================================
// These are reasonable defaults for a POC. In production, implied vol would
// come from the options chain and rates from a yield curve service.
MarketParams get_default_market_params(const std::string& ticker) {
    // Indian stocks — higher vol, higher risk-free rate (RBI repo rate ~6.5%)
    if (ticker == "RELIANCE")  return { 0.28, 0.065 };
    if (ticker == "TCS")       return { 0.22, 0.065 };
    if (ticker == "HDFCBANK")  return { 0.25, 0.065 };

    // US stocks — Fed funds rate ~5.25%
    if (ticker == "AAPL")  return { 0.25, 0.0525 };
    if (ticker == "TSLA")  return { 0.55, 0.0525 };  // Tesla historically high vol

    // Unknown ticker fallback
    return { 0.30, 0.05 };
}

// ============================================================================
// CSV Portfolio Loader
// ============================================================================
bool load_portfolio(const std::string& filepath, PortfolioSoA& portfolio) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[PORTFOLIO] CRITICAL: Could not open " << filepath << std::endl;
        return false;
    }

    portfolio.clear();

    std::string line;
    std::getline(file, line);  // Skip header: TICKER,POSITION_SIZE,STRIKE_PRICE,EXPIRY_DAYS,OPTION_TYPE

    // Count lines for pre-allocation (rewind and count)
    size_t line_count = 0;
    auto start_pos = file.tellg();
    while (std::getline(file, line)) ++line_count;
    file.clear();
    file.seekg(start_pos);

    portfolio.reserve(line_count);

    int loaded = 0;
    int skipped = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string ticker, pos_str, strike_str, expiry_str, type_str;

        std::getline(ss, ticker, ',');
        std::getline(ss, pos_str, ',');
        std::getline(ss, strike_str, ',');
        std::getline(ss, expiry_str, ',');
        std::getline(ss, type_str, ',');

        // Trim whitespace / carriage returns from type_str
        type_str.erase(std::remove(type_str.begin(), type_str.end(), '\r'), type_str.end());
        type_str.erase(std::remove(type_str.begin(), type_str.end(), ' '), type_str.end());

        try {
            int position_size = std::stoi(pos_str);
            double strike = std::stod(strike_str);
            int expiry_days = std::stoi(expiry_str);
            OptionType opt_type = parse_option_type(type_str);

            // Convert days to years for BSM
            double expiry_years = static_cast<double>(expiry_days) / 365.0;

            portfolio.tickers.push_back(ticker);
            portfolio.position_sizes.push_back(position_size);
            portfolio.strike_prices.push_back(strike);
            portfolio.expiry_years.push_back(expiry_years);
            portfolio.option_types.push_back(opt_type);
            portfolio.greeks.push_back({});  // Initialize empty, computed later

            ++loaded;
        } catch (const std::exception& e) {
            ++skipped;
            if (skipped <= 5) {
                std::cerr << "[PORTFOLIO] Skipped malformed row: " << line
                          << " (" << e.what() << ")" << std::endl;
            }
        }
    }

    file.close();

    std::cout << "[PORTFOLIO] Loaded " << loaded << " positions";
    if (skipped > 0) std::cout << " (skipped " << skipped << " malformed)";
    std::cout << std::endl;

    return loaded > 0;
}

// ============================================================================
// Per-Ticker Greek Recomputation
// ============================================================================
// When a new tick arrives for a ticker, recompute Greeks for every position
// in that ticker using the updated spot price.
int recompute_greeks_for_ticker(PortfolioSoA& portfolio,
                                const std::string& ticker,
                                double spot_price) {
    int updated = 0;
    MarketParams params = get_default_market_params(ticker);

    for (size_t i = 0; i < portfolio.size(); ++i) {
        if (portfolio.tickers[i] != ticker) continue;

        portfolio.greeks[i] = compute_greeks(
            spot_price,
            portfolio.strike_prices[i],
            params.risk_free_rate,
            params.implied_vol,
            portfolio.expiry_years[i],
            portfolio.option_types[i]
        );
        ++updated;
    }
    return updated;
}

// ============================================================================
// Portfolio-Level Greek Aggregation
// ============================================================================
PortfolioGreeks aggregate_greeks(const PortfolioSoA& portfolio,
                                 const std::string& ticker) {
    PortfolioGreeks agg{};
    agg.ticker = ticker.empty() ? "PORTFOLIO" : ticker;
    agg.net_delta = 0.0;
    agg.net_gamma = 0.0;
    agg.net_vega  = 0.0;
    agg.net_theta = 0.0;
    agg.net_rho   = 0.0;
    agg.total_notional = 0.0;
    agg.position_count = 0;

    for (size_t i = 0; i < portfolio.size(); ++i) {
        // Filter by ticker if specified
        if (!ticker.empty() && portfolio.tickers[i] != ticker) continue;

        double size = static_cast<double>(portfolio.position_sizes[i]);
        const auto& g = portfolio.greeks[i];

        // Weighted aggregation: Greek × position_size
        agg.net_delta += g.delta * size;
        agg.net_gamma += g.gamma * size;
        agg.net_vega  += g.vega  * size;
        agg.net_theta += g.theta * size;
        agg.net_rho   += g.rho   * size;

        // Notional = |position_size| × option_price (absolute exposure)
        agg.total_notional += std::fabs(size) * g.price;
        agg.position_count++;
    }

    return agg;
}

// ============================================================================
// Aggregate All Tickers
// ============================================================================
std::unordered_map<std::string, PortfolioGreeks> aggregate_all_greeks(
    const PortfolioSoA& portfolio) {

    // First, collect unique tickers
    std::unordered_map<std::string, PortfolioGreeks> result;

    for (size_t i = 0; i < portfolio.size(); ++i) {
        const auto& ticker = portfolio.tickers[i];
        if (result.find(ticker) == result.end()) {
            result[ticker] = aggregate_greeks(portfolio, ticker);
        }
    }

    return result;
}
