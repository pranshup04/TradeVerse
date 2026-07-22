#pragma once
// ============================================================================
// greeks.hpp — Black-Scholes-Merton Greek Computation Engine
// ============================================================================
// Provides deterministic, cache-friendly computation of first and second-order
// portfolio Greeks. All functions are designed for tight-loop vectorized calls
// over Struct-of-Arrays (SoA) portfolio layouts.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <string>

// Mathematical constants
constexpr double PI = 3.14159265358979323846;
constexpr double INV_SQRT_2PI = 0.3989422804014327; // 1 / sqrt(2 * PI)

// Option type enumeration
enum class OptionType : uint8_t {
    Call  = 0,
    Put   = 1,
    Stock = 2  // Underlying equity (Delta=1, all other Greeks=0)
};

// Result struct for a single position's Greeks
struct GreekResult {
    double delta;   // ∂V/∂S  — First-order price sensitivity
    double gamma;   // ∂²V/∂S² — Second-order price sensitivity (convexity)
    double vega;    // ∂V/∂σ  — Sensitivity to implied volatility
    double theta;   // ∂V/∂t  — Time decay (per calendar day)
    double rho;     // ∂V/∂r  — Sensitivity to risk-free rate
    double price;   // Theoretical BSM option price
};

// Aggregate portfolio-level Greeks for a single underlying
struct PortfolioGreeks {
    std::string ticker;
    double net_delta;
    double net_gamma;
    double net_vega;
    double net_theta;
    double net_rho;
    double total_notional;   // Sum of |position_size * spot_price|
    int    position_count;   // Number of positions contributing
};

// ============================================================================
// Core BSM Functions
// ============================================================================

// Standard normal PDF: φ(x) = (1/√2π) * e^(-x²/2)
double norm_pdf(double x);

// Standard normal CDF: Φ(x) — Abramowitz & Stegun approximation (|error| < 7.5e-8)
double norm_cdf(double x);

// Compute d1 and d2 for BSM formula
// d1 = [ln(S/K) + (r + σ²/2)·T] / (σ·√T)
// d2 = d1 - σ·√T
void compute_d1_d2(double S, double K, double r, double sigma, double T,
                   double& d1, double& d2);

// Compute all Greeks for a single option position
// S     = spot price
// K     = strike price
// r     = risk-free rate (annualized, e.g., 0.065 for 6.5%)
// sigma = implied/historical volatility (annualized)
// T     = time to expiry in years (e.g., 30 days = 30/365)
// type  = Call, Put, or Stock
GreekResult compute_greeks(double S, double K, double r, double sigma, double T,
                           OptionType type);

// Compute BSM option price only (fast path when Greeks not needed)
double bsm_price(double S, double K, double r, double sigma, double T,
                 OptionType type);

// ============================================================================
// Utility
// ============================================================================

// Parse option type string from CSV ("Call", "Put", "Stock") to enum
OptionType parse_option_type(const std::string& type_str);
