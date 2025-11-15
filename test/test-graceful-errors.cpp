// Copyright (c) 2020 Pantor. All rights reserved.

#include "inja/environment.hpp"

#include "test-common.hpp"

TEST_CASE("graceful error handling") {
  inja::Environment env;
  inja::json data;
  data["name"] = "Peter";
  data["age"] = 29;
  data["city"] = "Brunswick";

  SUBCASE("disabled by default") {
    // By default, missing variables should throw
    CHECK_THROWS_WITH(env.render("{{unknown}}", data), "[inja.exception.render_error] (at 1:3) variable 'unknown' not found");
    CHECK_THROWS_WITH(env.render("Hello {{ missing_var }}!", data), "[inja.exception.render_error] (at 1:10) variable 'missing_var' not found");
  }

  SUBCASE("enabled graceful errors") {
    env.set_graceful_errors(true);
    
    // Missing variables should render as original text
    CHECK(env.render("{{unknown}}", data) == "{{unknown}}");
    CHECK(env.render("Hello {{ missing_var }}!", data) == "Hello {{ missing_var }}!");
    CHECK(env.render("This prompt contains a {{ bad_variable }}!", data) == "This prompt contains a {{ bad_variable }}!");
    
    // Mixed valid and invalid variables
    CHECK(env.render("{{ name }} lives in {{ unknown_city }}", data) == "Peter lives in {{ unknown_city }}");
    CHECK(env.render("{{ unknown1 }} and {{ name }} and {{ unknown2 }}", data) == "{{ unknown1 }} and Peter and {{ unknown2 }}");
    
    // Valid variables should still work normally
    CHECK(env.render("Hello {{ name }}!", data) == "Hello Peter!");
    CHECK(env.render("{{ name }} is {{ age }} years old.", data) == "Peter is 29 years old.");
  }

  SUBCASE("error tracking") {
    env.set_graceful_errors(true);
    
    // Clear any previous errors
    env.clear_render_errors();
    
    // Render with missing variable
    auto result = env.render("Hello {{ missing_var }}!", data);
    CHECK(result == "Hello {{ missing_var }}!");
    
    // Check that error was tracked
    const auto& errors = env.get_last_render_errors();
    CHECK(errors.size() == 1);
    CHECK(errors[0].message == "variable 'missing_var' not found");
    CHECK(errors[0].original_text == "{{ missing_var }}");
    CHECK(errors[0].location.line == 1);
    CHECK(errors[0].location.column == 10);
  }

  SUBCASE("multiple errors") {
    env.set_graceful_errors(true);
    env.clear_render_errors();
    
    // Render with multiple missing variables
    auto result = env.render("{{ var1 }} and {{ name }} and {{ var2 }} and {{ var3 }}", data);
    CHECK(result == "{{ var1 }} and Peter and {{ var2 }} and {{ var3 }}");
    
    // Check that all errors were tracked
    const auto& errors = env.get_last_render_errors();
    CHECK(errors.size() == 3);
    CHECK(errors[0].message == "variable 'var1' not found");
    CHECK(errors[1].message == "variable 'var2' not found");
    CHECK(errors[2].message == "variable 'var3' not found");
  }

  SUBCASE("nested variables") {
    env.set_graceful_errors(true);
    
    inja::json nested_data;
    nested_data["user"]["name"] = "Alice";
    
    // Missing nested variable
    CHECK(env.render("{{ user.email }}", nested_data) == "{{ user.email }}");
    
    // Missing parent variable
    CHECK(env.render("{{ company.name }}", nested_data) == "{{ company.name }}");
    
    // Valid nested variable
    CHECK(env.render("{{ user.name }}", nested_data) == "Alice");
  }

  SUBCASE("graceful errors with whitespace") {
    env.set_graceful_errors(true);
    
    // Different whitespace patterns should be preserved
    CHECK(env.render("{{  unknown  }}", data) == "{{  unknown  }}");
    CHECK(env.render("{{ unknown}}", data) == "{{ unknown}}");
    CHECK(env.render("{{unknown }}", data) == "{{unknown }}");
  }

  SUBCASE("graceful errors in complex templates") {
    env.set_graceful_errors(true);
    
    std::string tmpl = R"(
Name: {{ name }}
Age: {{ age }}
Email: {{ email }}
City: {{ city }}
Country: {{ country }}
)";
    
    std::string expected = R"(
Name: Peter
Age: 29
Email: {{ email }}
City: Brunswick
Country: {{ country }}
)";
    
    CHECK(env.render(tmpl, data) == expected);
    
    const auto& errors = env.get_last_render_errors();
    CHECK(errors.size() == 2);
  }

  SUBCASE("error clearing") {
    env.set_graceful_errors(true);
    
    // First render with error
    env.render("{{ unknown }}", data);
    CHECK(env.get_last_render_errors().size() == 1);
    
    // Second render without error
    env.render("{{ name }}", data);
    CHECK(env.get_last_render_errors().size() == 0);
    
    // Third render with error again
    env.render("{{ unknown }}", data);
    CHECK(env.get_last_render_errors().size() == 1);
  }
}

