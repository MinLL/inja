// Copyright (c) 2024. Additional array manipulation functions for inja templates.

#ifndef INCLUDE_INJA_ARRAY_FUNCTIONS_HPP_
#define INCLUDE_INJA_ARRAY_FUNCTIONS_HPP_

#include "environment.hpp"
#include <algorithm>
#include <set>
#include <functional>

namespace inja {

/*!
 * @brief Register common array and object manipulation functions
 * These functions provide Jinja2-like array/list manipulation capabilities
 */
inline void register_array_functions(Environment& env) {
  
  // append(array, item) - Add item to end of array and return the array
  // Normal callback (makes a copy)
  auto append_callback = [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0]; // Return unchanged if not an array
    }
    json result = *args[0]; // Make a copy
    result.push_back(*args[1]);
    return result;
  };
  // In-place callback (mutates directly, used for self-assignment optimization)
  auto append_inplace = [](json& arr, Arguments& remaining_args) {
    if (arr.is_array() && !remaining_args.empty()) {
      arr.push_back(*remaining_args[0]);
    }
  };
  env.add_callback("append", 2, append_callback, append_inplace);

  // push(array, item) - Alias for append
  auto push_callback = [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    json result = *args[0];
    result.push_back(*args[1]);
    return result;
  };
  auto push_inplace = [](json& arr, Arguments& remaining_args) {
    if (arr.is_array() && !remaining_args.empty()) {
      arr.push_back(*remaining_args[0]);
    }
  };
  env.add_callback("push", 2, push_callback, push_inplace);

  // extend(array, items) - Add multiple items to array
  auto extend_callback = [](Arguments& args) {
    if (!args[0]->is_array() || !args[1]->is_array()) {
      return *args[0];
    }
    json result = *args[0];
    for (const auto& item : *args[1]) {
      result.push_back(item);
    }
    return result;
  };
  auto extend_inplace = [](json& arr, Arguments& remaining_args) {
    if (arr.is_array() && !remaining_args.empty() && remaining_args[0]->is_array()) {
      for (const auto& item : *remaining_args[0]) {
        arr.push_back(item);
      }
    }
  };
  env.add_callback("extend", 2, extend_callback, extend_inplace);

  // insert(array, index, item) - Insert item at specific position
  env.add_callback("insert", 3, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    json result = *args[0];
    try {
      int index = args[1]->get<int>();
      if (index < 0) {
        index = static_cast<int>(result.size()) + index;
      }
      if (index >= 0 && static_cast<size_t>(index) <= result.size()) {
        result.insert(result.begin() + index, *args[2]);
      }
    } catch (...) {
      // Return unchanged on error
    }
    return result;
  });

  // pop(array) or pop(array, index) - Remove and return item
  // Note: Since we can't return multiple values, just return the modified array
  env.add_callback("pop", 1, [](Arguments& args) {
    if (!args[0]->is_array() || args[0]->empty()) {
      return *args[0];
    }
    json result = *args[0];
    result.erase(result.end() - 1);
    return result;
  });

  env.add_callback("pop", 2, [](Arguments& args) {
    if (!args[0]->is_array() || args[0]->empty()) {
      return *args[0];
    }
    json result = *args[0];
    try {
      int index = args[1]->get<int>();
      if (index < 0) {
        index = static_cast<int>(result.size()) + index;
      }
      if (index >= 0 && static_cast<size_t>(index) < result.size()) {
        result.erase(result.begin() + index);
      }
    } catch (...) {
      // Return unchanged on error
    }
    return result;
  });

  // remove(array, value) - Remove first occurrence of value
  env.add_callback("remove", 2, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    json result = *args[0];
    auto it = std::find(result.begin(), result.end(), *args[1]);
    if (it != result.end()) {
      result.erase(it);
    }
    return result;
  });

  // clear(array) - Remove all items
  env.add_callback("clear", 1, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    return json::array();
  });

  // reverse(array) - Reverse array order
  env.add_callback("reverse", 1, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    json result = *args[0];
    std::reverse(result.begin(), result.end());
    return result;
  });

  // index(array, value) - Find index of value (-1 if not found)
  env.add_callback("index", 2, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return json(-1);
    }
    auto it = std::find(args[0]->begin(), args[0]->end(), *args[1]);
    if (it != args[0]->end()) {
      return json(static_cast<int>(std::distance(args[0]->begin(), it)));
    }
    return json(-1);
  });

  // count(array, value) - Count occurrences of value
  env.add_callback("count", 2, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return json(0);
    }
    return json(static_cast<int>(std::count(args[0]->begin(), args[0]->end(), *args[1])));
  });

  // unique(array) - Return array with duplicate values removed
  env.add_callback("unique", 1, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    json result = json::array();
    std::set<json> seen;
    for (const auto& item : *args[0]) {
      if (seen.insert(item).second) {
        result.push_back(item);
      }
    }
    return result;
  });

  // flatten(array, depth=1) - Flatten nested arrays
  env.add_callback("flatten", 1, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    json result = json::array();
    std::function<void(const json&, int)> flatten_helper = [&](const json& arr, int depth) {
      for (const auto& item : arr) {
        if (item.is_array() && depth > 0) {
          flatten_helper(item, depth - 1);
        } else {
          result.push_back(item);
        }
      }
    };
    flatten_helper(*args[0], 1);
    return result;
  });

  env.add_callback("flatten", 2, [](Arguments& args) {
    if (!args[0]->is_array()) {
      return *args[0];
    }
    json result = json::array();
    int max_depth = args[1]->get<int>();
    std::function<void(const json&, int)> flatten_helper = [&](const json& arr, int depth) {
      for (const auto& item : arr) {
        if (item.is_array() && depth > 0) {
          flatten_helper(item, depth - 1);
        } else {
          result.push_back(item);
        }
      }
    };
    flatten_helper(*args[0], max_depth);
    return result;
  });

  // Object/Dict functions

  // update(object, other) - Merge two objects
  env.add_callback("update", 2, [](Arguments& args) {
    if (!args[0]->is_object()) {
      return *args[0];
    }
    json result = *args[0];
    if (args[1]->is_object()) {
      result.update(*args[1]);
    }
    return result;
  });

  // keys(object) - Get array of object keys
  env.add_callback("keys", 1, [](Arguments& args) {
    if (!args[0]->is_object()) {
      return json::array();
    }
    json result = json::array();
    for (auto it = args[0]->begin(); it != args[0]->end(); ++it) {
      result.push_back(it.key());
    }
    return result;
  });

  // values(object) - Get array of object values
  env.add_callback("values", 1, [](Arguments& args) {
    if (!args[0]->is_object()) {
      return json::array();
    }
    json result = json::array();
    for (auto it = args[0]->begin(); it != args[0]->end(); ++it) {
      result.push_back(it.value());
    }
    return result;
  });

  // items(object) - Get array of [key, value] pairs
  env.add_callback("items", 1, [](Arguments& args) {
    if (!args[0]->is_object()) {
      return json::array();
    }
    json result = json::array();
    for (auto it = args[0]->begin(); it != args[0]->end(); ++it) {
      result.push_back(json::array({it.key(), it.value()}));
    }
    return result;
  });

  // get(object, key, default=null) - Get value with default
  env.add_callback("get", 2, [](Arguments& args) {
    if (args[0]->is_object() && args[1]->is_string()) {
      auto key = args[1]->get<std::string>();
      if (args[0]->contains(key)) {
        return (*args[0])[key];
      }
    }
    return json(nullptr);
  });

  env.add_callback("get", 3, [](Arguments& args) {
    if (args[0]->is_object() && args[1]->is_string()) {
      auto key = args[1]->get<std::string>();
      if (args[0]->contains(key)) {
        return (*args[0])[key];
      }
    }
    return *args[2]; // Return default value
  });

  // has_key(object, key) - Check if object has key
  env.add_callback("has_key", 2, [](Arguments& args) {
    if (args[0]->is_object() && args[1]->is_string()) {
      return json(args[0]->contains(args[1]->get<std::string>()));
    }
    return json(false);
  });
}

} // namespace inja

#endif // INCLUDE_INJA_ARRAY_FUNCTIONS_HPP_

