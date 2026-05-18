#pragma once

#include <string>

namespace trader::nba {

// Arcsine "safe lead" win-probability for NBA games, derived from
// Clauset/Kogan/Redner (2015, Phys Rev E, arXiv:1503.03509).
//
// Treats the score differential as a 1D random walk with zero drift, scoring-
// event variance fit to NBA data (σ ≈ 0.4602 in units of points/√sec for
// 90%-confidence safe leads). The "safe lead" L_safe(t, α) is the deficit
// at which the trailing team retains probability (1 − α) of NOT catching up
// before the clock expires.
//
// Two complementary calls:
//
//   1) safe_lead(t, alpha) — points needed at remaining-time `t` (seconds) to
//      hold a lead with confidence `1 − alpha`. e.g. safe_lead(720, 0.10)
//      returns the 90%-safe lead at start of Q4 (~12.4 pts).
//
//   2) survival_probability(lead, t) — probability the leading team WINS,
//      given an integer-or-fractional point lead and `t` seconds remaining.
//      Derived from the same random-walk model via the complementary error
//      function. Returns 0.5 when lead == 0 or t == 0 (handled at boundaries).
//
// Limitations the caller must respect:
//   - Assumes equal-strength teams. Apply an offset for known spread before
//     calling: lead_adjusted = lead - pregame_spread_pts * (time_elapsed/48).
//   - Final ~60 seconds breaks the diffusion assumption (intentional fouling,
//     stoppages, 3-point heaves). Use a separate hand-tuned table there.
//   - Garbage time mid-quarter is well-modelled; large leads in early quarters
//     are not (random walk under-represents talent gaps over long horizons).
class WinProbability {
public:
    // Underlying random-walk volatility of the NBA score differential, in
    // points per √second. Derived to make the 90% safe-lead formula match the
    // empirical Clauset/Kogan/Redner fit `L = 0.4602 × √t`:
    //
    //   safe_lead(t, 0.10) = z(0.90) × σ × √t   with z(0.90) ≈ 1.28155
    //   ⇒ σ = 0.4602 / 1.28155 ≈ 0.3591 pts/√sec
    //
    // This σ also makes survival_probability() internally consistent with
    // safe_lead() across all (t, alpha) — see the SafeLeadAtThatProbability
    // test for the round-trip check.
    static constexpr double kSigmaPointsPerSqrtSec = 0.3591;

    // Safe lead in points to hold with confidence (1 - alpha) given t seconds
    // remaining in regulation. alpha=0.10 → 90%-safe; alpha=0.05 → 95%-safe.
    static double safe_lead(double t_seconds_remaining, double alpha);

    // Probability the leading team wins, given an integer point lead and
    // t seconds remaining. Symmetric: pass negative lead for the trailing
    // team's WIN probability. Returns 0.5 on degenerate inputs.
    static double survival_probability(double lead_points,
                                        double t_seconds_remaining);

    // Inverse normal CDF for use in safe_lead. Acklam's rational approximation;
    // accuracy ~1.15e-9 across the central 99.99% of the distribution. Public
    // for direct testing.
    static double inverse_normal_cdf(double p);
};

} // namespace trader::nba
