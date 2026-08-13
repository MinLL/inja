#ifndef INCLUDE_INJA_UTILS_HPP_
#define INCLUDE_INJA_UTILS_HPP_

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "exceptions.hpp"
#include "json.hpp"

namespace inja {

namespace string_view {
inline std::string_view slice(std::string_view view, size_t start, size_t end) {
  start = std::min(start, view.size());
  end = std::min(std::max(start, end), view.size());
  return view.substr(start, end - start);
}

inline std::pair<std::string_view, std::string_view> split(std::string_view view, char Separator) {
  const size_t idx = view.find(Separator);
  if (idx == std::string_view::npos) {
    return std::make_pair(view, std::string_view());
  }
  return std::make_pair(slice(view, 0, idx), slice(view, idx + 1, std::string_view::npos));
}

inline bool starts_with(std::string_view view, std::string_view prefix) {
  return (view.size() >= prefix.size() && view.compare(0, prefix.size(), prefix) == 0);
}
} // namespace string_view

inline SourceLocation get_source_location(std::string_view content, size_t pos) {
  // Get line and offset position (starts at 1:1)
  auto sliced = string_view::slice(content, 0, pos);
  const std::size_t last_newline = sliced.rfind('\n');

  if (last_newline == std::string_view::npos) {
    return {1, sliced.length() + 1};
  }

  // Count newlines
  size_t count_lines = 0;
  size_t search_start = 0;
  while (search_start <= sliced.size()) {
    search_start = sliced.find('\n', search_start) + 1;
    if (search_start == 0) {
      break;
    }
    count_lines += 1;
  }

  return {count_lines + 1, sliced.length() - last_newline};
}

/*!
@brief Folds a single byte to lower case, ASCII range only.

Deliberately not std::tolower: that folds according to the active C locale, so a
process running under a single-byte locale such as cp1251 or cp1252 would also fold
bytes 0xC0-0xDE - exactly the UTF-8 lead-byte range - and make distinct non-ASCII
keys compare equal. This keeps the folding independent of the ambient locale.
*/
inline char fold_ascii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

/*!
@brief Compares two strings for equality, ignoring ASCII case.

Only ASCII letters are folded, so UTF-8 keys are compared byte-for-byte beyond
the ASCII range and are never mangled.
*/
inline bool equals_ignore_case(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i += 1) {
    if (fold_ascii(a[i]) != fold_ascii(b[i])) {
      return false;
    }
  }
  return true;
}

/*!
@brief Looks up an object member by key, falling back to a case-insensitive match.

An exact match always wins and costs a single hash/tree lookup. Only when the exact
lookup misses do we scan the members for a case-insensitive match, returning the
first one found. Returns nullptr if the value is not an object or nothing matches.
*/
inline const json* find_member_ignore_case(const json& object, const std::string& key) {
  if (!object.is_object()) {
    return nullptr;
  }

  const auto exact = object.find(key);
  if (exact != object.end()) {
    return &(*exact);
  }

  for (auto member = object.cbegin(); member != object.cend(); ++member) {
    if (equals_ignore_case(member.key(), key)) {
      return &member.value();
    }
  }
  return nullptr;
}

/*!
@brief Resolves a dot-separated variable name (e.g. "actor.name"), ignoring key case.

Walks the name one segment at a time: object segments go through
find_member_ignore_case, array segments accept an all-digit index. Returns nullptr
as soon as a segment cannot be resolved.

The dotted name is walked rather than the equivalent json_pointer because
json_pointer offers no forward token iteration, and because DataNode builds its
pointer without escaping '~' or '/' - the raw name is the more faithful source.
*/
inline const json* find_by_dotted_name_ignore_case(const json& root, std::string_view dotted_name) {
  const json* current = &root;
  std::string_view remaining = dotted_name;

  do {
    std::string_view part;
    std::tie(part, remaining) = string_view::split(remaining, '.');

    if (current->is_object()) {
      current = find_member_ignore_case(*current, std::string(part));
    } else if (current->is_array()) {
      // from_chars is locale independent, rejects signs and trailing junk, and reports
      // out-of-range instead of wrapping - so it accepts exactly the digit-only segments
      // that name a reachable index.
      size_t index = 0;
      const auto parsed = std::from_chars(part.data(), part.data() + part.size(), index);
      const bool is_index = parsed.ec == std::errc() && parsed.ptr == part.data() + part.size();
      current = (is_index && index < current->size()) ? &(*current)[index] : nullptr;
    } else {
      return nullptr;
    }

    if (current == nullptr) {
      return nullptr;
    }
  } while (!remaining.empty());

  return current;
}

inline void replace_substring(std::string& s, const std::string& f, const std::string& t) {
  if (f.empty()) {
    return;
  }
  for (auto pos = s.find(f);            // find first occurrence of f
       pos != std::string::npos;        // make sure f was found
       s.replace(pos, f.size(), t),     // replace with t, and
       pos = s.find(f, pos + t.size())) // find next occurrence of f
  {}
}

} // namespace inja

#endif // INCLUDE_INJA_UTILS_HPP_
