#include "core/dashboard.hpp"
#include <iostream>

namespace trader {

Dashboard::Dashboard(const RiskManager& risk, const KillSwitch& kill,
                       const kalshi::CalibrationLogger& calibration,
                       const kalshi::KalshiExchange& exchange)
    : risk_(risk), kill_(kill), calibration_(calibration), exchange_(exchange),
      start_time_(std::chrono::system_clock::now()) {}

std::string Dashboard::format_uptime() const {
    auto now = std::chrono::system_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    int hours = static_cast<int>(dur.count() / 3600);
    int mins = static_cast<int>((dur.count() % 3600) / 60);
    int secs = static_cast<int>(dur.count() % 60);
    std::ostringstream oss;
    oss << hours << "h " << mins << "m " << secs << "s";
    return oss.str();
}

std::string Dashboard::format_dollars(double v) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (v >= 0) oss << "$" << v;
    else oss << "-$" << (-v);
    return oss.str();
}

std::string Dashboard::render() const {
    std::ostringstream out;

    std::string mode = kill_.is_active() ? "KILLED" : "RUNNING";
    std::string risk_status = kill_.is_active() ? "KILLED" :
                              (risk_.daily_pnl() < -risk_.config().max_daily_loss * 0.8) ? "WARNING" : "OK";

    out << "========================================\n";
    out << " KALSHI EVENT TRADER  " << mode << "  Uptime: " << format_uptime() << "\n";
    out << "========================================\n";
    out << " Balance: " << format_dollars(risk_.balance())
        << "  Exposure: " << format_dollars(risk_.total_exposure())
        << "  Available: " << format_dollars(risk_.available_capital()) << "\n";
    out << " Daily PnL: " << format_dollars(risk_.daily_pnl())
        << "  Total PnL: " << format_dollars(calibration_.total_pnl()) << "\n";
    out << "----------------------------------------\n";

    // Positions
    auto positions = exchange_.get_all_positions();
    out << " POSITIONS (" << positions.size() << " active)\n";
    if (positions.empty()) {
        out << "  (none)\n";
    } else {
        for (const auto& pos : positions) {
            out << "  " << pos.ticker << "  " << pos.contract_side
                << "  " << pos.quantity << "x @ " << format_dollars(pos.avg_cost) << "\n";
        }
    }
    out << "----------------------------------------\n";

    // Model performance
    auto brier = calibration_.brier_score();
    auto weather_brier = calibration_.brier_score("weather");
    auto econ_brier = calibration_.brier_score("economics");

    out << " MODEL PERFORMANCE\n";
    out << "  Brier Score: " << std::fixed << std::setprecision(3) << brier.score
        << " (" << brier.num_predictions << " predictions)\n";
    if (weather_brier.num_predictions > 0) {
        out << "  Weather: " << weather_brier.score << " (" << weather_brier.num_predictions << ")\n";
    }
    if (econ_brier.num_predictions > 0) {
        out << "  Economics: " << econ_brier.score << " (" << econ_brier.num_predictions << ")\n";
    }
    out << "  Signals: " << signal_count_ << "  Trades: " << trade_count_
        << "  Resolved: " << calibration_.resolved_trades() << "\n";
    out << "----------------------------------------\n";

    // Risk status
    out << " RISK: " << risk_status;
    if (kill_.is_active()) {
        out << " (" << kill_.reason() << ")";
    }
    out << "\n";
    out << " [Q]uit  [K]ill  [R]efresh  [C]alibration\n";
    out << "========================================\n";

    return out.str();
}

void Dashboard::display() const {
    // Clear screen (ANSI escape code — works on Windows 10+)
    std::cout << "\033[2J\033[H";
    std::cout << render();
    std::cout.flush();
}

} // namespace trader
