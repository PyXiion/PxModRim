#pragma once

#include <expected>

#include <QString>

namespace px::core {

struct Error {
    QString message;
};

template <class T>
using Result = std::expected<T, Error>;

}  // namespace px::core
