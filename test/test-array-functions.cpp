// Copyright (c) 2024. Tests for array manipulation functions.

#include "inja/environment.hpp"
#include "inja/array_functions.hpp"

#include "test-common.hpp"

TEST_CASE("array manipulation functions") {
  inja::Environment env;
  inja::register_array_functions(env);
  
  inja::json data;
  data["items"] = inja::json::array({1, 2, 3});
  data["obj"] = inja::json::object({{"name", "Alice"}, {"age", 30}});

  SUBCASE("append function") {
    // Append to array
    CHECK(env.render("{{ append(items, 4) }}", data) == "[1,2,3,4]");
    CHECK(env.render("{{ append([], 1) }}", data) == "[1]");
    
    // Can use in set statements
    auto result = env.render("{% set newItems = append(items, 5) %}{{ newItems }}", data);
    CHECK(result.find("5") != std::string::npos);
  }

  SUBCASE("push function (alias for append)") {
    CHECK(env.render("{{ push(items, 4) }}", data) == "[1,2,3,4]");
    CHECK(env.render("{{ push([], 1) }}", data) == "[1]");
  }

  SUBCASE("extend function") {
    CHECK(env.render("{{ extend(items, [4, 5]) }}", data) == "[1,2,3,4,5]");
    CHECK(env.render("{{ extend([], [1, 2]) }}", data) == "[1,2]");
  }

  SUBCASE("insert function") {
    CHECK(env.render("{{ insert(items, 0, 0) }}", data) == "[0,1,2,3]");
    CHECK(env.render("{{ insert(items, 2, 99) }}", data) == "[1,2,99,3]");
    CHECK(env.render("{{ insert(items, -1, 99) }}", data) == "[1,2,99,3]");
  }

  SUBCASE("pop function") {
    CHECK(env.render("{{ pop(items) }}", data) == "[1,2]");
    CHECK(env.render("{{ pop(items, 0) }}", data) == "[2,3]");
    CHECK(env.render("{{ pop(items, 1) }}", data) == "[1,3]");
    CHECK(env.render("{{ pop(items, -1) }}", data) == "[1,2]");
  }

  SUBCASE("remove function") {
    CHECK(env.render("{{ remove(items, 2) }}", data) == "[1,3]");
    CHECK(env.render("{{ remove(items, 99) }}", data) == "[1,2,3]"); // No change if not found
  }

  SUBCASE("clear function") {
    CHECK(env.render("{{ clear(items) }}", data) == "[]");
  }

  SUBCASE("reverse function") {
    CHECK(env.render("{{ reverse(items) }}", data) == "[3,2,1]");
    CHECK(env.render("{{ reverse([]) }}", data) == "[]");
  }

  SUBCASE("index function") {
    CHECK(env.render("{{ index(items, 2) }}", data) == "1");
    CHECK(env.render("{{ index(items, 99) }}", data) == "-1");
  }

  SUBCASE("count function") {
    data["duplicates"] = inja::json::array({1, 2, 2, 3, 2});
    CHECK(env.render("{{ count(duplicates, 2) }}", data) == "3");
    CHECK(env.render("{{ count(duplicates, 99) }}", data) == "0");
  }

  SUBCASE("unique function") {
    data["duplicates"] = inja::json::array({1, 2, 2, 3, 1, 3});
    auto result = env.render("{{ unique(duplicates) | length }}", data);
    CHECK(result == "3");
  }

  SUBCASE("flatten function") {
    data["nested"] = inja::json::array({1, inja::json::array({2, 3}), 4});
    CHECK(env.render("{{ flatten(nested) | length }}", data) == "4");
    
    data["deep"] = inja::json::array({1, inja::json::array({2, inja::json::array({3, 4})})});
    CHECK(env.render("{{ flatten(deep, 1) | length }}", data) == "3");
    CHECK(env.render("{{ flatten(deep, 2) | length }}", data) == "4");
  }

  SUBCASE("update function") {
    data["obj2"] = inja::json::object({{"age", 31}, {"city", "NYC"}});
    auto result = env.render("{{ update(obj, obj2) }}", data);
    CHECK(result.find("\"age\":31") != std::string::npos);
    CHECK(result.find("\"city\":\"NYC\"") != std::string::npos);
  }

  SUBCASE("keys function") {
    auto result = env.render("{{ keys(obj) | length }}", data);
    CHECK(result == "2");
  }

  SUBCASE("values function") {
    auto result = env.render("{{ values(obj) | length }}", data);
    CHECK(result == "2");
  }

  SUBCASE("items function") {
    auto result = env.render("{{ items(obj) | length }}", data);
    CHECK(result == "2");
  }

  SUBCASE("get function") {
    CHECK(env.render("{{ get(obj, \"name\") }}", data) == "Alice");
    CHECK(env.render("{{ get(obj, \"missing\") }}", data) == "");
    CHECK(env.render("{{ get(obj, \"missing\", \"default\") }}", data) == "default");
  }

  SUBCASE("has_key function") {
    CHECK(env.render("{{ has_key(obj, \"name\") }}", data) == "true");
    CHECK(env.render("{{ has_key(obj, \"missing\") }}", data) == "false");
  }
}

TEST_CASE("array functions in complex templates") {
  inja::Environment env;
  env.set_graceful_errors(true);
  inja::register_array_functions(env);
  
  inja::json data;
  data["items"] = inja::json::array();

  SUBCASE("building arrays with append in template") {
    std::string tmpl = R"(
{% set myArray = [] %}
{% set myArray = append(myArray, {"name": "Item1", "value": 10}) %}
{% set myArray = append(myArray, {"name": "Item2", "value": 20}) %}
{% set myArray = append(myArray, {"name": "Item3", "value": 30}) %}

Count: {{ myArray | length }}
{% for item in myArray %}
- {{ item.name }}: {{ item.value }}
{% endfor %}
)";
    
    auto result = env.render(tmpl, data);
    CHECK(result.find("Count: 3") != std::string::npos);
    CHECK(result.find("Item1") != std::string::npos);
    CHECK(result.find("Item2") != std::string::npos);
    CHECK(result.find("Item3") != std::string::npos);
  }

  SUBCASE("conditional array building") {
    std::string tmpl = R"(
{% set active = [] %}
{% set check1 = true %}
{% set check2 = false %}
{% set check3 = true %}

{% if check1 %}
  {% set active = append(active, "Feature1") %}
{% endif %}
{% if check2 %}
  {% set active = append(active, "Feature2") %}
{% endif %}
{% if check3 %}
  {% set active = append(active, "Feature3") %}
{% endif %}

Active: {{ active | length }}
{% for feature in active %}
- {{ feature }}
{% endfor %}
)";
    
    auto result = env.render(tmpl, data);
    CHECK(result.find("Active: 2") != std::string::npos);
    CHECK(result.find("Feature1") != std::string::npos);
    CHECK(result.find("Feature3") != std::string::npos);
    CHECK(result.find("Feature2") == std::string::npos);
  }

  SUBCASE("array manipulation chain") {
    std::string tmpl = R"(
{% set nums = [1, 2, 3] %}
{% set nums = append(nums, 4) %}
{% set nums = append(nums, 5) %}
{% set nums = reverse(nums) %}
Result: {{ nums }}
)";
    
    auto result = env.render(tmpl, data);
    CHECK(result.find("[5,4,3,2,1]") != std::string::npos);
  }

  SUBCASE("object manipulation") {
    std::string tmpl = R"(
{% set person = {"name": "Alice"} %}
{% set extra = {"age": 30, "city": "NYC"} %}
{% set person = update(person, extra) %}
Keys: {{ keys(person) | length }}
Has age: {{ has_key(person, "age") }}
)";
    
    auto result = env.render(tmpl, data);
    CHECK(result.find("Keys: 3") != std::string::npos);
    CHECK(result.find("Has age: true") != std::string::npos);
  }

  SUBCASE("graceful handling of operations on non-arrays") {
    std::string tmpl = R"(
{% set notArray = "string" %}
{% set result = append(notArray, "item") %}
Result: {{ result }}
)";

    CHECK_NOTHROW(env.render(tmpl, data));
    auto result = env.render(tmpl, data);
    CHECK(result.find("string") != std::string::npos);
  }
}

TEST_CASE("self-assignment in-place optimization") {
  inja::Environment env;
  inja::register_array_functions(env);

  inja::json data;

  SUBCASE("in-place optimization with callback wrapper for tracing") {
    // Verify that the callback wrapper is still invoked with in-place optimization
    int append_call_count = 0;
    env.set_callback_wrapper([&](const std::string& name,
                                  const inja::Arguments& args,
                                  const std::function<inja::json()>& thunk) {
      if (name == "append") {
        append_call_count++;
      }
      return thunk();
    });

    std::string tmpl = R"(
{% set items = [] %}
{% set items = append(items, 1) %}
{% set items = append(items, 2) %}
{% set items = append(items, 3) %}
{{ items }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("[1,2,3]") != std::string::npos);
    CHECK(append_call_count == 3);  // All three append calls should be traced
  }

  SUBCASE("append self-assignment produces correct result") {
    // The optimization should produce the same result as without
    std::string tmpl = R"(
{% set items = [] %}
{% set items = append(items, 1) %}
{% set items = append(items, 2) %}
{% set items = append(items, 3) %}
{{ items }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("[1,2,3]") != std::string::npos);
  }

  SUBCASE("push self-assignment produces correct result") {
    std::string tmpl = R"(
{% set items = [] %}
{% set items = push(items, "a") %}
{% set items = push(items, "b") %}
{% set items = push(items, "c") %}
{{ items }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("[\"a\",\"b\",\"c\"]") != std::string::npos);
  }

  SUBCASE("extend self-assignment produces correct result") {
    std::string tmpl = R"(
{% set items = [1] %}
{% set items = extend(items, [2, 3]) %}
{% set items = extend(items, [4, 5]) %}
{{ items }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("[1,2,3,4,5]") != std::string::npos);
  }

  SUBCASE("non-self-assignment still works") {
    // When assigning to a different variable, normal copy behavior
    std::string tmpl = R"(
{% set original = [1, 2] %}
{% set copy = append(original, 3) %}
Original: {{ original }}
Copy: {{ copy }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("Original: [1,2]") != std::string::npos);
    CHECK(result.find("Copy: [1,2,3]") != std::string::npos);
  }

  SUBCASE("self-assignment with complex items") {
    std::string tmpl = R"(
{% set items = [] %}
{% set items = append(items, {"name": "Item1", "value": 100}) %}
{% set items = append(items, {"name": "Item2", "value": 200}) %}
Count: {{ items | length }}
First: {{ items.0.name }}
Second: {{ items.1.value }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("Count: 2") != std::string::npos);
    CHECK(result.find("First: Item1") != std::string::npos);
    CHECK(result.find("Second: 200") != std::string::npos);
  }

  SUBCASE("self-assignment in loop") {
    data["source"] = inja::json::array({1, 2, 3, 4, 5});

    std::string tmpl = R"(
{% set evens = [] %}
{% for item in source %}
  {% if item % 2 == 0 %}
    {% set evens = append(evens, item) %}
  {% endif %}
{% endfor %}
Evens: {{ evens }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("[2,4]") != std::string::npos);
  }

  SUBCASE("nested property self-assignment falls back to copy") {
    // Nested property access like foo.bar should fall back to normal path
    // since our detection only handles simple variable names
    std::string tmpl = R"(
{% set foo = {"items": [1, 2]} %}
{% set foo.items = append(foo.items, 3) %}
{{ foo.items }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("[1,2,3]") != std::string::npos);
  }

  SUBCASE("expression as first argument falls back to copy") {
    // When first arg is not a simple variable, use normal path
    std::string tmpl = R"(
{% set items = [] %}
{% set items = append(reverse(items), 1) %}
{{ items }}
)";

    // This should still work, just using the copy path
    auto result = env.render(tmpl, data);
    CHECK(result.find("[1]") != std::string::npos);
  }

  SUBCASE("large array building performance") {
    // Build a large array to test optimization effectiveness
    // This test verifies correctness; actual performance improvement
    // should be measured with profiling/tracing
    std::string tmpl = R"(
{% set items = [] %}
{% for i in range(100) %}
  {% set items = append(items, i) %}
{% endfor %}
Count: {{ items | length }}
First: {{ items.0 }}
Last: {{ items.99 }}
)";

    auto result = env.render(tmpl, data);
    CHECK(result.find("Count: 100") != std::string::npos);
    CHECK(result.find("First: 0") != std::string::npos);
    CHECK(result.find("Last: 99") != std::string::npos);
  }
}

