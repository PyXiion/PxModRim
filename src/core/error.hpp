#pragma once

#include <QString>
#include <expected>

namespace px::modrim {

struct Error {
  QString message;
};

template <class T>
using Result = std::expected<T, Error>;

}  // namespace px::modrim
