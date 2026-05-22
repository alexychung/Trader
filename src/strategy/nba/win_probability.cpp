#include "strategy/nba/win_probability.hpp"

#include <algorithm>
#include <cmath>

namespace trader::nba {

double WinProbability::inverse_normal_cdf(double p) {
    // Acklam's rational approximation to the inverse normal CDF.
    // Defensive clamp — callers should not pass p outside (0, 1) but handle it.
    p = std::clamp(p, 1e-12, 1.0 - 1e-12);

    static constexpr double a[] = {
        -3.969683028665376e+01,  2.209460984245205e+02,
        -2.759285104469687e+02,  1.383577518672690e+02,
        -3.066479806614716e+01,  2.506628277459239e+00};
    static constexpr double b[] = {
        -5.447609879822406e+01,  1.615858368580409e+02,
        -1.556989798598866e+02,  6.680131188771972e+01,
        -1.328068155288572e+01};
    static constexpr double c[] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
        4.374664141464968e+00,  2.938163982698783e+00};
    static constexpr double d[] = {
        7.784695709041462e-03,  3.224671290700398e-01,
        2.445134137142996e+00,  3.754408661907416e+00};

    const double plow  = 0.02425;
    const double phigh = 1.0 - plow;

    if (p < plow) {
        double q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
               ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }
    if (p > phigh) {
        double q = std::sqrt(-2.0 * std::log(1.0 - p));
        return -(((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
                ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }
    double q = p - 0.5;
    double r = q * q;
    return (((((a[0]*r + a[1])*r + a[2])*r + a[3])*r + a[4])*r + a[5]) * q /
           (((((b[0]*r + b[1])*r + b[2])*r + b[3])*r + b[4])*r + 1.0);
}

double WinProbability::safe_lead(double t_seconds_remaining, double alpha) {
    if (t_seconds_remaining <= 0.0) return 0.0;
    if (alpha <= 0.0) alpha = 1e-9;
    if (alpha >= 1.0) alpha = 1.0 - 1e-9;
    const double z = inverse_normal_cdf(1.0 - alpha);
    return z * kSigmaPointsPerSqrtSec * std::sqrt(t_seconds_remaining);
}

double WinProbability::survival_probability(double lead_points,
                                              double t_seconds_remaining) {
    if (t_seconds_remaining <= 0.0) {
        if (lead_points > 0.0) return 1.0;
        if (lead_points < 0.0) return 0.0;
        return 0.5;
    }
    if (lead_points == 0.0) return 0.5;

    // P(survive) = 1 - 0.5 * erfc(lead / (σ * √(2t)))  for lead > 0
    // Equivalently:  0.5 * (1 + erf(lead / (σ * √(2t))))
    const double denom = kSigmaPointsPerSqrtSec * std::sqrt(2.0 * t_seconds_remaining);
    return 0.5 * (1.0 + std::erf(lead_points / denom));
}

// === Logit-space helpers ===

double WinProbability::price_to_logit(double p) {
    constexpr double eps = 1e-9;
    p = std::clamp(p, eps, 1.0 - eps);
    return std::log(p / (1.0 - p));
}

double WinProbability::logit_to_price(double L) {
    return 1.0 / (1.0 + std::exp(-L));
}

double WinProbability::logit_reservation_price(double p_mid, int inventory,
                                                 double gamma,
                                                 double sigma_logit,
                                                 double t_seconds_remaining) {
    if (t_seconds_remaining <= 0.0 || inventory == 0) return p_mid;
    const double L_mid = price_to_logit(p_mid);
    const double L_r = L_mid - static_cast<double>(inventory) * gamma *
                                  sigma_logit * sigma_logit *
                                  t_seconds_remaining;
    return logit_to_price(L_r);
}

} // namespace trader::nba
