#pragma once
// ============================================================================
// portfolio.hpp — Struct-of-Arrays Portfolio Data Structure
// ============================================================================
// Cache-optimal SoA layout for vectorized Greek computation over 10,000+
// positions. Each field is a contiguous array — minimizes cache misses when
// iterating over a single attribute across all positions.
// ============================================================================

#include <vector>
#include <string>
#include <unordered_map>
#include "greeks.hpp"

// ============================================================================
// SoA Portfolio Layout
// ============================================================================
struct PortfolioSoA {
    // Position metadata (parallel arrays — index i is the same position across all)
    std::vector<std::string> tickers;
    std::vector<int>         position_sizes;   // Negative = short/sold
    std::vector<double>      strike_prices;
    std::vector<double>      expiry_years;     // Time to expiry in years (converted from days)
    std::vector<OptionType>  option_types;

    // Derived risk data (populated after Greek computation)
    std::vector<GreekResult> greeks;           // Per-position Greek results

    // Portfolio size
    size_t size() const { return tickers.size(); }

    // Reserve memory upfront (avoids reallocation during CSV parsing)
    void reserve(size_t n) {
        tickers.reserve(n);
        position_sizes.reserve(n);
        strike_prices.reserve(n);
        expiry_years.reserve(n);
        option_types.reserve(n);
        greeks.reserve(n);
    }

    // Clear all data
    void clear() {
        tickers.clear();
        position_sizes.clear();
        strike_prices.clear();
        expiry_years.clear();
        option_types.clear();
        greeks.clear();
    }
};

// ============================================================================
// Default volatility assumptions per ticker
// In production, these would come from a vol surface or implied vol feed.
// Using annualized historical vol estimates as reasonable defaults.
// ============================================================================
struct MarketParams {
    double implied_vol;      // Annualized implied volatility (σ)
    double risk_free_rate;   // Annualized risk-free rate (r)
};

// Returns default market parameters for a given ticker
// US stocks: r = 5.25% (Fed funds rate), India: r = 6.5% (RBI repo rate)
MarketParams get_default_market_params(const std::string& ticker);

// ============================================================================
// Portfolio I/O
// ============================================================================

// Load portfolio from CSV file into SoA structure
// Expected format: TICKER,POSITION_SIZE,STRIKE_PRICE,EXPIRY_DAYS,OPTION_TYPE
// Returns true on success, false on failure
bool load_portfolio(const std::string& filepath, PortfolioSoA& portfolio);

// ============================================================================
// Portfolio-Level Risk Computation
// ============================================================================

// Recompute Greeks for all positions matching a given ticker at a new spot price
// Returns the number of positions updated
int recompute_greeks_for_ticker(PortfolioSoA& portfolio,
                                const std::string& ticker,
                                double spot_price);

// Aggregate portfolio Greeks across all positions (or filtered by ticker)
// If ticker is empty, aggregates across entire portfolio
PortfolioGreeks aggregate_greeks(const PortfolioSoA& portfolio,
                                 const std::string& ticker = "");

// Aggregate Greeks for all unique tickers in the portfolio
// Returns a map of ticker -> PortfolioGreeks
std::unordered_map<std::string, PortfolioGreeks> aggregate_all_greeks(
    const PortfolioSoA& portfolio);
