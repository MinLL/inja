// Copyright (c) 2020 Pantor. All rights reserved.

#include "inja/environment.hpp"

#include "test-common.hpp"

TEST_CASE("variable crash tests - missing nested properties") {
  inja::Environment env;
  env.set_graceful_errors(true);
  
  inja::json data;
  data["good"] = inja::json::object();
  data["good"]["exists"] = "value";
  data["user"] = inja::json::object();
  data["user"]["name"] = "Alice";
  data["user"]["profile"] = inja::json::object();
  data["user"]["profile"]["age"] = 30;

  SUBCASE("single level missing property") {
    // Object exists, but property doesn't
    CHECK_NOTHROW(env.render("{{ good.bad }}", data));
    CHECK(env.render("{{ good.bad }}", data) == "{{ good.bad }}");
    
    CHECK_NOTHROW(env.render("{{ user.email }}", data));
    CHECK(env.render("{{ user.email }}", data) == "{{ user.email }}");
  }

  SUBCASE("double nested missing property - good.bad.bad") {
    // Object exists, but nested property doesn't exist (the reported crash case)
    CHECK_NOTHROW(env.render("{{ good.bad.bad }}", data));
    CHECK(env.render("{{ good.bad.bad }}", data) == "{{ good.bad.bad }}");
    
    // Mix of existing and missing nested properties
    CHECK_NOTHROW(env.render("{{ user.profile.missing }}", data));
    CHECK(env.render("{{ user.profile.missing }}", data) == "{{ user.profile.missing }}");
  }

  SUBCASE("triple nested missing property") {
    // Test even deeper nesting: good.bad.worse.worst
    CHECK_NOTHROW(env.render("{{ good.bad.worse.worst }}", data));
    CHECK(env.render("{{ good.bad.worse.worst }}", data) == "{{ good.bad.worse.worst }}");
    
    CHECK_NOTHROW(env.render("{{ user.profile.address.street }}", data));
    CHECK(env.render("{{ user.profile.address.street }}", data) == "{{ user.profile.address.street }}");
  }

  SUBCASE("very deep nested missing properties") {
    // Test 5+ levels deep
    CHECK_NOTHROW(env.render("{{ a.b.c.d.e.f.g }}", data));
    CHECK(env.render("{{ a.b.c.d.e.f.g }}", data) == "{{ a.b.c.d.e.f.g }}");
    
    CHECK_NOTHROW(env.render("{{ good.x.y.z.w.q }}", data));
    CHECK(env.render("{{ good.x.y.z.w.q }}", data) == "{{ good.x.y.z.w.q }}");
  }

  SUBCASE("mixed existing and missing properties in chain") {
    // Start with existing, then go missing
    CHECK_NOTHROW(env.render("{{ user.name.length }}", data));
    CHECK(env.render("{{ user.name.length }}", data) == "{{ user.name.length }}");
    
    CHECK_NOTHROW(env.render("{{ user.profile.age.toString }}", data));
    CHECK(env.render("{{ user.profile.age.toString }}", data) == "{{ user.profile.age.toString }}");
    
    // Valid property followed by missing nested ones
    CHECK_NOTHROW(env.render("{{ good.exists.nested.deep }}", data));
    CHECK(env.render("{{ good.exists.nested.deep }}", data) == "{{ good.exists.nested.deep }}");
  }

  SUBCASE("missing root variable with nested access") {
    // Variable doesn't exist at all, but we try to access nested properties
    CHECK_NOTHROW(env.render("{{ nonexistent.property }}", data));
    CHECK(env.render("{{ nonexistent.property }}", data) == "{{ nonexistent.property }}");
    
    CHECK_NOTHROW(env.render("{{ missing.a.b.c }}", data));
    CHECK(env.render("{{ missing.a.b.c }}", data) == "{{ missing.a.b.c }}");
  }

  SUBCASE("array-like access on missing properties") {
    // Try to access array indices on missing properties
    CHECK_NOTHROW(env.render("{{ good.bad.0 }}", data));
    CHECK(env.render("{{ good.bad.0 }}", data) == "{{ good.bad.0 }}");
    
    CHECK_NOTHROW(env.render("{{ missing.items.0.name }}", data));
    CHECK(env.render("{{ missing.items.0.name }}", data) == "{{ missing.items.0.name }}");
  }

  SUBCASE("nested property access in expressions") {
    // Use missing nested properties in conditionals
    CHECK_NOTHROW(env.render("{% if good.bad.bad %}yes{% else %}no{% endif %}", data));
    CHECK(env.render("{% if good.bad.bad %}yes{% else %}no{% endif %}", data) == "no");
    
    // Use missing nested properties in loops
    std::string loop_tmpl = "{% for item in good.bad.items %}{{ item }}{% endfor %}Done";
    CHECK_NOTHROW(env.render(loop_tmpl, data));
    CHECK(env.render(loop_tmpl, data) == "Done");
  }

  SUBCASE("nested property access in filters") {
    // Apply filters to missing nested properties
    CHECK_NOTHROW(env.render("{{ good.bad.bad | upper }}", data));
    
    CHECK_NOTHROW(env.render("{{ user.missing.field | replace(\"_\", \" \") }}", data));
  }

  SUBCASE("nested property access with at() function") {
    // Use at() function on missing nested properties
    CHECK_NOTHROW(env.render("{{ at(good.bad, \"key\") }}", data));
    
    CHECK_NOTHROW(env.render("{{ at(user.missing, \"field\") }}", data));
  }

  SUBCASE("operations on missing nested properties") {
    // Arithmetic operations
    CHECK_NOTHROW(env.render("{{ good.bad.value + 10 }}", data));
    
    // Comparison operations
    CHECK_NOTHROW(env.render("{{ good.bad.count > 5 }}", data));
    
    // String concatenation
    CHECK_NOTHROW(env.render("{{ \"prefix\" + good.bad.suffix }}", data));
  }

  SUBCASE("set statement with missing nested properties") {
    // Try to set a variable to a missing nested property
    CHECK_NOTHROW(env.render("{% set x = good.bad.bad %}{{ x }}", data));
    
    CHECK_NOTHROW(env.render("{% set result = user.missing.field %}Result: {{ result }}", data));
  }
}

TEST_CASE("variable crash tests - special variable name 'name'") {
  inja::Environment env;
  env.set_graceful_errors(true);
  
  inja::json data;
  data["name"] = "TestName";
  data["user"] = inja::json::object();
  data["user"]["name"] = "UserName";
  data["item"] = inja::json::object();
  data["item"]["id"] = 123;

  SUBCASE("variable literally called 'name'") {
    // Test that 'name' works as a regular variable
    CHECK_NOTHROW(env.render("{{ name }}", data));
    CHECK(env.render("{{ name }}", data) == "TestName");
    
    // Test nested 'name' property
    CHECK_NOTHROW(env.render("{{ user.name }}", data));
    CHECK(env.render("{{ user.name }}", data) == "UserName");
  }

  SUBCASE("missing 'name' property") {
    // Test accessing missing 'name' property on an object that doesn't have it
    CHECK_NOTHROW(env.render("{{ item.name }}", data));
    CHECK(env.render("{{ item.name }}", data) == "{{ item.name }}");
  }

  SUBCASE("nested missing with 'name' in chain") {
    // Test chains involving 'name'
    CHECK_NOTHROW(env.render("{{ user.name.length }}", data));
    CHECK(env.render("{{ user.name.length }}", data) == "{{ user.name.length }}");
    
    CHECK_NOTHROW(env.render("{{ item.name.value }}", data));
    CHECK(env.render("{{ item.name.value }}", data) == "{{ item.name.value }}");
    
    // Deep nesting with 'name'
    CHECK_NOTHROW(env.render("{{ obj.name.nested.name }}", data));
    CHECK(env.render("{{ obj.name.nested.name }}", data) == "{{ obj.name.nested.name }}");
  }

  SUBCASE("'name' in loops and conditionals") {
    inja::json loop_data;
    loop_data["items"] = inja::json::array({
      {{"name", "Item1"}, {"id", 1}},
      {{"name", "Item2"}, {"id", 2}},
      {{"id", 3}} // This one is missing 'name'
    });
    
    // Loop accessing 'name' property, some items have it, some don't
    std::string tmpl = "{% for item in items %}Name: {{ item.name }}, ID: {{ item.id }}; {% endfor %}";
    CHECK_NOTHROW(env.render(tmpl, loop_data));
    auto result = env.render(tmpl, loop_data);
    CHECK(result.find("Name: Item1") != std::string::npos);
    CHECK(result.find("Name: Item2") != std::string::npos);
    CHECK(result.find("Name: {{ item.name }}") != std::string::npos); // Missing name
  }

  SUBCASE("'name' with filters and functions") {
    // Apply filters to 'name'
    CHECK_NOTHROW(env.render("{{ name | upper }}", data));
    CHECK(env.render("{{ name | upper }}", data) == "TESTNAME");
    
    // Apply filters to missing 'name'
    CHECK_NOTHROW(env.render("{{ item.name | lower }}", data));
    
    // Use 'name' in function calls
    CHECK_NOTHROW(env.render("{{ at(item, \"name\") }}", data));
  }
}

TEST_CASE("variable crash tests - edge cases with null and empty objects") {
  inja::Environment env;
  env.set_graceful_errors(true);
  
  inja::json data;
  data["empty_obj"] = inja::json::object();
  data["null_val"] = nullptr;
  data["empty_array"] = inja::json::array();
  data["nested"] = inja::json::object();
  data["nested"]["empty"] = inja::json::object();

  SUBCASE("accessing properties on empty objects") {
    CHECK_NOTHROW(env.render("{{ empty_obj.property }}", data));
    CHECK(env.render("{{ empty_obj.property }}", data) == "{{ empty_obj.property }}");
    
    CHECK_NOTHROW(env.render("{{ empty_obj.a.b.c }}", data));
    CHECK(env.render("{{ empty_obj.a.b.c }}", data) == "{{ empty_obj.a.b.c }}");
  }

  SUBCASE("accessing properties on null values") {
    CHECK_NOTHROW(env.render("{{ null_val.property }}", data));
    CHECK(env.render("{{ null_val.property }}", data) == "{{ null_val.property }}");
    
    CHECK_NOTHROW(env.render("{{ null_val.a.b.c }}", data));
    CHECK(env.render("{{ null_val.a.b.c }}", data) == "{{ null_val.a.b.c }}");
  }

  SUBCASE("accessing properties on arrays") {
    CHECK_NOTHROW(env.render("{{ empty_array.length }}", data));
    CHECK(env.render("{{ empty_array.length }}", data) == "{{ empty_array.length }}");
    
    CHECK_NOTHROW(env.render("{{ empty_array.property.nested }}", data));
    CHECK(env.render("{{ empty_array.property.nested }}", data) == "{{ empty_array.property.nested }}");
  }

  SUBCASE("deeply nested empty objects") {
    CHECK_NOTHROW(env.render("{{ nested.empty.property }}", data));
    CHECK(env.render("{{ nested.empty.property }}", data) == "{{ nested.empty.property }}");
    
    CHECK_NOTHROW(env.render("{{ nested.empty.a.b.c }}", data));
    CHECK(env.render("{{ nested.empty.a.b.c }}", data) == "{{ nested.empty.a.b.c }}");
  }
}

TEST_CASE("variable crash tests - type mismatches") {
  inja::Environment env;
  env.set_graceful_errors(true);
  
  inja::json data;
  data["number"] = 42;
  data["string"] = "hello";
  data["boolean"] = true;
  data["array"] = inja::json::array({1, 2, 3});

  SUBCASE("accessing properties on primitives") {
    // Numbers
    CHECK_NOTHROW(env.render("{{ number.property }}", data));
    CHECK(env.render("{{ number.property }}", data) == "{{ number.property }}");
    
    CHECK_NOTHROW(env.render("{{ number.a.b.c }}", data));
    CHECK(env.render("{{ number.a.b.c }}", data) == "{{ number.a.b.c }}");
    
    // Strings
    CHECK_NOTHROW(env.render("{{ string.property }}", data));
    CHECK(env.render("{{ string.property }}", data) == "{{ string.property }}");
    
    CHECK_NOTHROW(env.render("{{ string.nested.deep }}", data));
    CHECK(env.render("{{ string.nested.deep }}", data) == "{{ string.nested.deep }}");
    
    // Booleans
    CHECK_NOTHROW(env.render("{{ boolean.property }}", data));
    CHECK(env.render("{{ boolean.property }}", data) == "{{ boolean.property }}");
    
    CHECK_NOTHROW(env.render("{{ boolean.x.y.z }}", data));
    CHECK(env.render("{{ boolean.x.y.z }}", data) == "{{ boolean.x.y.z }}");
  }

  SUBCASE("array element access on non-arrays") {
    // Try to access array indices on non-array types
    CHECK_NOTHROW(env.render("{{ number.0 }}", data));
    CHECK(env.render("{{ number.0 }}", data) == "{{ number.0 }}");
    
    CHECK_NOTHROW(env.render("{{ string.0.property }}", data));
    CHECK(env.render("{{ string.0.property }}", data) == "{{ string.0.property }}");
  }
}

TEST_CASE("variable crash tests - complex real-world scenarios") {
  inja::Environment env;
  env.set_graceful_errors(true);
  
  inja::json data;
  data["user"] = inja::json::object();
  data["user"]["name"] = "Alice";
  data["items"] = inja::json::array();

  SUBCASE("complex template with multiple missing nested accesses") {
    std::string tmpl = R"(
User: {{ user.name }}
Email: {{ user.email }}
Address: {{ user.address.street }}
City: {{ user.address.city.name }}
Postal: {{ user.address.postal.code }}
Profile: {{ user.profile.bio.text }}
)";
    
    CHECK_NOTHROW(env.render(tmpl, data));
    auto result = env.render(tmpl, data);
    CHECK(result.find("User: Alice") != std::string::npos);
    CHECK(result.find("{{ user.email }}") != std::string::npos);
    CHECK(result.find("{{ user.address.street }}") != std::string::npos);
    CHECK(result.find("{{ user.address.city.name }}") != std::string::npos);
  }

  SUBCASE("nested loops with missing properties") {
    inja::json complex_data;
    complex_data["departments"] = inja::json::array({
      {{"name", "Engineering"}, {"employees", inja::json::array({
        {{"name", "Bob"}},
        {{"id", 123}} // missing name
      })}},
      {{"name", "Sales"}} // missing employees
    });
    
    std::string tmpl = R"(
{% for dept in departments %}
Department: {{ dept.name }}
{% for emp in dept.employees %}
  Employee: {{ emp.name }} ({{ emp.email }})
{% endfor %}
{% endfor %}
)";
    
    CHECK_NOTHROW(env.render(tmpl, complex_data));
  }

  SUBCASE("conditional chains with missing nested properties") {
    std::string tmpl = R"(
{% if user.settings.notifications.email %}
  Email notifications enabled
{% else if user.settings.notifications.sms %}
  SMS notifications enabled
{% else if user.settings.alerts.desktop %}
  Desktop alerts enabled
{% else %}
  No notifications configured
{% endif %}
)";
    
    CHECK_NOTHROW(env.render(tmpl, data));
    auto result = env.render(tmpl, data);
    CHECK(result.find("No notifications configured") != std::string::npos);
  }

  SUBCASE("set statements creating nested structures") {
    std::string tmpl = R"(
{% set cache = user.cache.data %}
{% set pref = user.preferences.theme.dark %}
{% set backup = system.backup.latest.file %}
Result: done
)";
    
    CHECK_NOTHROW(env.render(tmpl, data));
    auto result = env.render(tmpl, data);
    CHECK(result.find("Result: done") != std::string::npos);
  }

  SUBCASE("filters on deeply nested missing properties") {
    // All filters should now handle missing/null values gracefully
    std::string tmpl = R"(
Name: {{ user.profile.display_name | upper }}
Bio: {{ user.profile.bio.text | replace("_", " ") }}
Tags: {{ user.profile.tags.list | join(", ") }}
Score: {{ user.stats.score.value | round(2) }}
)";
    
    CHECK_NOTHROW(env.render(tmpl, data));
    
    // String filters work with null/missing gracefully
    CHECK_NOTHROW(env.render("{{ user.missing | upper }}", data));
    CHECK_NOTHROW(env.render("{{ user.missing | lower }}", data));
    
    // Numeric filters should also work gracefully now
    CHECK_NOTHROW(env.render("{{ user.stats.score.value | round(2) }}", data));
    CHECK_NOTHROW(env.render("{{ user.count.value | float }}", data));
    CHECK_NOTHROW(env.render("{{ user.id.value | int }}", data));
  }
}

TEST_CASE("variable crash tests - without graceful errors") {
  inja::Environment env;
  // Graceful errors disabled - should throw exceptions
  
  inja::json data;
  data["good"] = inja::json::object();
  data["good"]["exists"] = "value";

  SUBCASE("missing nested properties should throw") {
    // These should throw exceptions when graceful errors is disabled
    CHECK_THROWS_WITH(env.render("{{ good.bad }}", data), 
                     "[inja.exception.render_error] (at 1:4) variable 'good.bad' not found");
    
    CHECK_THROWS_AS(env.render("{{ good.bad.bad }}", data), inja::RenderError);
    
    CHECK_THROWS_AS(env.render("{{ missing.a.b.c }}", data), inja::RenderError);
  }

  SUBCASE("operations on missing nested should throw") {
    CHECK_THROWS_AS(env.render("{{ good.bad.value + 10 }}", data), inja::RenderError);
    
    CHECK_THROWS_AS(env.render("{{ good.missing | upper }}", data), inja::RenderError);
  }
}

TEST_CASE("variable crash tests - stress test with many levels") {
  inja::Environment env;
  env.set_graceful_errors(true);
  
  inja::json data;
  data["root"] = inja::json::object();

  SUBCASE("10 levels deep missing properties") {
    std::string tmpl = "{{ root.a.b.c.d.e.f.g.h.i.j }}";
    CHECK_NOTHROW(env.render(tmpl, data));
    CHECK(env.render(tmpl, data) == "{{ root.a.b.c.d.e.f.g.h.i.j }}");
  }

  SUBCASE("15 levels deep missing properties") {
    std::string tmpl = "{{ root.l1.l2.l3.l4.l5.l6.l7.l8.l9.l10.l11.l12.l13.l14.l15 }}";
    CHECK_NOTHROW(env.render(tmpl, data));
    CHECK(env.render(tmpl, data) == "{{ root.l1.l2.l3.l4.l5.l6.l7.l8.l9.l10.l11.l12.l13.l14.l15 }}");
  }

  SUBCASE("multiple deep accesses in same template") {
    std::string tmpl = R"(
{{ a.b.c.d.e }}
{{ x.y.z.w.q }}
{{ p1.p2.p3.p4.p5 }}
{{ m.n.o.p.q.r.s.t }}
)";
    CHECK_NOTHROW(env.render(tmpl, data));
  }
}

TEST_CASE("variable crash tests - comprehensive filter tests") {
  inja::Environment env;
  env.set_graceful_errors(true);
  
  inja::json data;
  data["user"] = inja::json::object();
  data["user"]["name"] = "Alice";
  data["items"] = inja::json::array({1, 2, 3});

  SUBCASE("all string filters handle null gracefully") {
    CHECK_NOTHROW(env.render("{{ user.missing | upper }}", data));
    CHECK_NOTHROW(env.render("{{ user.missing | lower }}", data));
    CHECK_NOTHROW(env.render("{{ user.missing.nested | capitalize }}", data));
    CHECK_NOTHROW(env.render("{{ user.a.b.c | replace(\"x\", \"y\") }}", data));
  }
  
  SUBCASE("all numeric filters handle null gracefully") {
    // These now work - numeric filters handle null gracefully
    CHECK_NOTHROW(env.render("{{ user.missing | round(2) }}", data));
    CHECK_NOTHROW(env.render("{{ user.missing.value | round(2) }}", data));
    CHECK_NOTHROW(env.render("{{ user.stats.score.value | round(2) }}", data));
    CHECK_NOTHROW(env.render("{{ user.count | float }}", data));
    CHECK_NOTHROW(env.render("{{ user.id | int }}", data));
    CHECK_NOTHROW(env.render("{{ user.value | even }}", data));
    CHECK_NOTHROW(env.render("{{ user.value | odd }}", data));
  }
  
  SUBCASE("array filters handle null gracefully") {
    CHECK_NOTHROW(env.render("{{ user.tags | length }}", data));
    CHECK_NOTHROW(env.render("{{ user.items | first }}", data));
    CHECK_NOTHROW(env.render("{{ user.items | last }}", data));
    CHECK_NOTHROW(env.render("{{ user.numbers | max }}", data));
    CHECK_NOTHROW(env.render("{{ user.numbers | min }}", data));
    CHECK_NOTHROW(env.render("{{ user.list | sort }}", data));
    CHECK_NOTHROW(env.render("{{ user.tags | join(\", \") }}", data));
  }
  
  SUBCASE("math operations handle null gracefully") {
    CHECK_NOTHROW(env.render("{{ user.value / 2 }}", data));
    CHECK_NOTHROW(env.render("{{ user.a.b % 5 }}", data));
    CHECK_NOTHROW(env.render("{{ user.missing ^ 2 }}", data));
  }
  
  SUBCASE("other filters handle null gracefully") {
    CHECK_NOTHROW(env.render("{{ user.count | divisibleBy(3) }}", data));
    CHECK_NOTHROW(env.render("{{ user.value | range }}", data));
    
    // exists checks should work
    CHECK_NOTHROW(env.render("{% if exists(\"user.missing\") %}yes{% else %}no{% endif %}", data));
    CHECK_NOTHROW(env.render("{% if existsIn(user, \"missing\") %}yes{% else %}no{% endif %}", data));
  }
  
  SUBCASE("type-checking filters work on null") {
    CHECK_NOTHROW(env.render("{% if user.missing | isNumber %}yes{% else %}no{% endif %}", data));
    CHECK_NOTHROW(env.render("{% if user.missing | isString %}yes{% else %}no{% endif %}", data));
    CHECK_NOTHROW(env.render("{% if user.missing | isArray %}yes{% else %}no{% endif %}", data));
    CHECK_NOTHROW(env.render("{% if user.missing | isObject %}yes{% else %}no{% endif %}", data));
    CHECK_NOTHROW(env.render("{% if user.missing | isBoolean %}yes{% else %}no{% endif %}", data));
  }
  
  SUBCASE("default filter provides fallback for missing values") {
    // The default filter provides a clean way to handle missing values
    CHECK_NOTHROW(env.render("{{ default(user.stats.score.value, 0) | round(2) }}", data));
    CHECK(env.render("{{ default(user.missing, \"fallback\") }}", data) == "fallback");
    CHECK(env.render("{{ default(user.name, \"fallback\") }}", data) == "Alice");
  }
}

