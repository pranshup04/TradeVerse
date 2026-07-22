// ============================================================================
// greeks.cpp — Black-Scholes-Merton Greek Engine Implementation
// ============================================================================
// All computations use closed-form BSM solutions. No heap allocations in the
// hot path. Designed for tight-loop calls over thousands of positions.
// ============================================================================

#include "greeks.hpp"
#include <algorithm>
#include <stdexcept>

// ============================================================================
// Standard Normal PDF
// φ(x) = (1/√2π) * e^(-x²/2)
// ============================================================================
double norm_pdf(double x) {
    return INV_SQRT_2PI * std::exp(-0.5 * x * x);
}

// ============================================================================
// Standard Normal CDF — Abramowitz & Stegun Approximation (Formula 26.2.17)
// Maximum absolute error: 7.5e-8
// This avoids std::erfc which can have platform-dependent performance.
// ============================================================================
double norm_cdf(double x) {
    if (x < -10.0) return 0.0;
    if (x >  10.0) return 1.0;

    // Constants for the rational approximation
    constexpr double a1 =  0.254829592;
    constexpr double a2 = -0.284496736;
    constexpr double a3 =  1.421413741;
    constexpr double a4 = -1.453152027;
    constexpr double a5 =  1.061405429;
    constexpr double p  =  0.3275911;

    // Compute using |x| and adjust sign at the end
    double abs_x = std::fabs(x);
    double t = 1.0 / (1.0 + p * abs_x);
    double t2 = t * t;
    double t3 = t2 * t;
    double t4 = t3 * t;
    double t5 = t4 * t;

    double poly = a1 * t + a2 * t2 + a3 * t3 + a4 * t4 + a5 * t5;
    double result = 1.0 - poly * norm_pdf(abs_x);

    return (x >= 0.0) ? result : (1.0 - result);
}

// ============================================================================
// BSM d1 and d2 Computation
// d1 = [ln(S/K) + (r + σ²/2)·T] / (σ·√T)
// d2 = d1 - σ·√T
// ============================================================================
void compute_d1_d2(double S, double K, double r, double sigma, double T,
                   double& d1, double& d2) {
    double sigma_sqrt_t = sigma * std::sqrt(T);
    d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / sigma_sqrt_t;
    d2 = d1 - sigma_sqrt_t;
}

// ============================================================================
// Full Greek Computation for a Single Option
// ============================================================================
GreekResult compute_greeks(double S, double K, double r, double sigma, double T,
                           OptionType type) {
    GreekResult result{};

    // Stock positions have trivial Greeks
    if (type == OptionType::Stock) {
        result.delta = 1.0;
        result.gamma = 0.0;
        result.vega  = 0.0;
        result.theta = 0.0;
        result.rho   = 0.0;
        result.price = S;
        return result;
    }

    // Guard against degenerate inputs
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0 || K <= 0.0) {
        // At or past expiry — intrinsic value only
        if (type == OptionType::Call) {
            result.delta = (S > K) ? 1.0 : 0.0;
            result.price = std::max(S - K, 0.0);
        } else {
            result.delta = (S < K) ? -1.0 : 0.0;
            result.price = std::max(K - S, 0.0);
        }
        return result;
    }

    // Core BSM variables
    double d1, d2;
    compute_d1_d2(S, K, r, sigma, T, d1, d2);

    double Nd1  = norm_cdf(d1);
    double Nd2  = norm_cdf(d2);
    double nd1  = norm_pdf(d1);
    double sqrtT = std::sqrt(T);
    double exp_rT = std::exp(-r * T);

    // ===================== CALL OPTIONS =====================
    if (type == OptionType::Call) {
        // Price: C = S·N(d1) - K·e^(-rT)·N(d2)
        result.price = S * Nd1 - K * exp_rT * Nd2;

        // Delta: ∂C/∂S = N(d1)
        result.delta = Nd1;

        // Gamma: ∂²C/∂S² = φ(d1) / (S·σ·√T)  [same for calls and puts]
        result.gamma = nd1 / (S * sigma * sqrtT);

        // Vega: ∂C/∂σ = S·φ(d1)·√T  [same for calls and puts]
        // Returned per 1% vol move (divide by 100) — industry convention
        result.vega = S * nd1 * sqrtT * 0.01;

        // Theta: ∂C/∂t (per calendar day, negative = decay)
        // Θ = -[S·φ(d1)·σ / (2√T)] - r·K·e^(-rT)·N(d2)
        result.theta = (-(S * nd1 * sigma) / (2.0 * sqrtT)
                        - r * K * exp_rT * Nd2) / 365.0;

        // Rho: ∂C/∂r = K·T·e^(-rT)·N(d2) (per 1% rate move)
        result.rho = K * T * exp_rT * Nd2 * 0.01;
    }
    // ===================== PUT OPTIONS =====================
    else {
        double Nmd1 = norm_cdf(-d1);  // N(-d1)
        double Nmd2 = norm_cdf(-d2);  // N(-d2)

        // Price: P = K·e^(-rT)·N(-d2) - S·N(-d1)
        result.price = K * exp_rT * Nmd2 - S * Nmd1;

        // Delta: ∂P/∂S = N(d1) - 1 = -N(-d1)
        result.delta = Nd1 - 1.0;

        // Gamma: same as call
        result.gamma = nd1 / (S * sigma * sqrtT);

        // Vega: same as call
        result.vega = S * nd1 * sqrtT * 0.01;

        // Theta: Θ = -[S·φ(d1)·σ / (2√T)] + r·K·e^(-rT)·N(-d2)
        result.theta = (-(S * nd1 * sigma) / (2.0 * sqrtT)
                        + r * K * exp_rT * Nmd2) / 365.0;

        // Rho: ∂P/∂r = -K·T·e^(-rT)·N(-d2) (per 1% rate move)
        result.rho = -K * T * exp_rT * Nmd2 * 0.01;
    }

    return result;
}

// ============================================================================
// BSM Price Only (Fast Path)
// ============================================================================
double bsm_price(double S, double K, double r, double sigma, double T,
                 OptionType type) {
    if (type == OptionType::Stock) return S;
    if (T <= 0.0 || sigma <= 0.0) {
        if (type == OptionType::Call) return std::max(S - K, 0.0);
        return std::max(K - S, 0.0);
    }

    double d1, d2;
    compute_d1_d2(S, K, r, sigma, T, d1, d2);
    double exp_rT = std::exp(-r * T);

    if (type == OptionType::Call) {
        return S * norm_cdf(d1) - K * exp_rT * norm_cdf(d2);
    }
    return K * exp_rT * norm_cdf(-d2) - S * norm_cdf(-d1);
}

// ============================================================================
// Utility: Parse option type string to enum
// ============================================================================
OptionType parse_option_type(const std::string& type_str) {
    if (type_str == "Call")  return OptionType::Call;
    if (type_str == "Put")   return OptionType::Put;
    if (type_str == "Stock") return OptionType::Stock;
    throw std::runtime_error("Unknown option type: " + type_str);
}
