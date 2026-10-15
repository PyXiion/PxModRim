#pragma once

#include <cstdint>

namespace px::core {

struct CollectionStats {
    std::int64_t total = 0;
    std::int64_t active = 0;
    std::int64_t inactive = 0;
    std::int64_t errors = 0;
};

}  // namespace px::core
