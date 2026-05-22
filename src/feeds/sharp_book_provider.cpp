#include "feeds/sharp_book_provider.hpp"

// Currently only the NullSharpBookProvider is implemented — it lives inline
// in the header. This translation unit exists so the build system can pull
// the symbol; future OddsApiPinnacleProvider, BettingProsProvider, etc.
// will be added here without churning the header for every new backend.

namespace trader::feeds {
// (intentionally empty — header-only stub for now)
} // namespace trader::feeds
