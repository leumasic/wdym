module;

#include <ankerl/unordered_dense.h>

module wdym:containers;

import std;

namespace {
namespace unordered_dense = ankerl::unordered_dense;
}  // namespace

namespace wdym {
struct StringHash : unordered_dense::hash<std::string_view> {
  // To enable heterogenous lookups with string_view
  using is_transparent = void;  // NOLINT
};

template <typename T>
using StringKeyMap = unordered_dense::map<std::string, T, StringHash, std::equal_to<>>;

using StringKeySet = unordered_dense::set<std::string, StringHash, std::equal_to<>>;

template <typename T, typename U>
  requires std::same_as<std::remove_cvref_t<U>, T>
void pushBackIfEmptyOrNotEqualToLast(std::vector<T>& elements, U&& value) {
  if (elements.empty() || elements.back() != value)
    elements.push_back(std::forward<U>(value));
}
}  // namespace wdym
