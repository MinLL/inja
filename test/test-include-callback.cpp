// Copyright (c) 2020 Pantor. All rights reserved.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "inja/environment.hpp"

#include "test-common.hpp"

// The include callback may hand inja a Template that carries only raw content - no pre-parsed
// AST. inja then parses that content itself, exactly as it does for templates it loaded from
// disk, so nested {% include %} / {% extends %} resolve recursively through the same callback.
// This lets an embedder resolve templates out of its own store without ever re-entering
// Environment::parse() from inside the callback.
TEST_CASE("content-returning include callback") {
  std::map<std::string, std::string> files;
  std::vector<std::string> requested;

  inja::Environment env;
  env.set_search_included_templates_in_files(false);
  env.set_include_callback([&files, &requested](const std::filesystem::path&, const std::string& name) {
    requested.push_back(name);
    const auto it = files.find(name);
    if (it == files.end()) {
      INJA_THROW(inja::FileError("failed accessing file at '" + name + "'"));
    }
    return inja::Template(it->second);
  });

  inja::json data;
  data["name"] = "Peter";

  SUBCASE("raw content is parsed and rendered") {
    files["greet"] = "Hello {{ name }}!";

    CHECK(env.render("{% include \"greet\" %}", data) == "Hello Peter!");
    CHECK(requested.size() == 1);
  }

  SUBCASE("nested includes resolve recursively") {
    files["level1"] = "L1[{% include \"level2\" %}]";
    files["level2"] = "L2[{% include \"level3\" %}]";
    files["level3"] = "L3";

    CHECK(env.render("{% include \"level1\" %}", data) == "L1[L2[L3]]");
    CHECK(requested.size() == 3);
  }

  SUBCASE("extends resolves through the callback") {
    files["ext_base"] = "<{% block body %}default{% endblock %}>";

    CHECK(env.render("{% extends \"ext_base\" %}{% block body %}child{% endblock %}", data) == "<child>");
  }

  SUBCASE("extends chains resolve recursively") {
    files["chain_base"] = "BASE[{% block body %}base-body{% endblock %}]";
    files["chain_mid"] = "{% extends \"chain_base\" %}";

    CHECK(env.render("{% extends \"chain_mid\" %}{% block body %}leaf-body{% endblock %}", data) == "BASE[leaf-body]");
    CHECK(requested.size() == 2);
  }

  SUBCASE("a template included twice is only fetched once") {
    files["diamond_leaf"] = "L";
    files["diamond_a"] = "A{% include \"diamond_leaf\" %}";
    files["diamond_b"] = "B{% include \"diamond_leaf\" %}";

    CHECK(env.render("{% include \"diamond_a\" %}{% include \"diamond_b\" %}", data) == "ALBL");
    CHECK(std::count(requested.begin(), requested.end(), std::string("diamond_leaf")) == 1);
  }

  SUBCASE("nested content still sees variables and functions") {
    files["loop"] = "{% for i in range(3) %}{{ i }}{% endfor %}-{{ upper(name) }}";

    CHECK(env.render("{% include \"loop\" %}", data) == "012-PETER");
  }

  SUBCASE("an unresolvable include still propagates the callback's error") {
    CHECK_THROWS_AS(env.render("{% include \"nope\" %}", data), inja::FileError);
  }
}

// The callback also runs as the fallback when file search is enabled but the file is missing.
TEST_CASE("content-returning include callback as file-search fallback") {
  inja::Environment env;
  env.set_search_included_templates_in_files(true);
  env.set_include_callback([](const std::filesystem::path&, const std::string& name) {
    if (name == "fallback_missing_from_disk") {
      return inja::Template("F[{% include \"fallback_nested\" %}]");
    }
    if (name == "fallback_nested") {
      return inja::Template("N");
    }
    INJA_THROW(inja::FileError("failed accessing file at '" + name + "'"));
  });

  const inja::json data;
  CHECK(env.render("{% include \"fallback_missing_from_disk\" %}", data) == "F[N]");
}

// Backwards compatibility: callbacks that return an already-parsed Template keep working
// unchanged - the returned AST is stored as-is and never parsed a second time.
TEST_CASE("pre-parsed include callback is left untouched") {
  std::map<std::string, std::string> files;

  inja::Environment env;
  env.set_search_included_templates_in_files(false);
  env.set_include_callback([&files, &env](const std::filesystem::path&, const std::string& name) {
    const auto it = files.find(name);
    if (it == files.end()) {
      INJA_THROW(inja::FileError("failed accessing file at '" + name + "'"));
    }
    return env.parse(it->second);
  });

  inja::json data;
  data["name"] = "Peter";

  SUBCASE("content is rendered exactly once") {
    files["legacy_greet"] = "Hi {{ name }}!";

    CHECK(env.render("{% include \"legacy_greet\" %}", data) == "Hi Peter!");
  }

  SUBCASE("includes nested inside pre-parsed content still resolve") {
    files["legacy_outer"] = "O[{% include \"legacy_inner\" %}]";
    files["legacy_inner"] = "I";

    CHECK(env.render("{% include \"legacy_outer\" %}", data) == "O[I]");
  }

  SUBCASE("an empty pre-parsed template renders as nothing") {
    files["legacy_empty"] = "";

    CHECK(env.render("[{% include \"legacy_empty\" %}]", data) == "[]");
  }
}
