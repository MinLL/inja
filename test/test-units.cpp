// Copyright (c) 2020 Pantor. All rights reserved.

#include <clocale>
#include <string>

#include "inja/environment.hpp"

#include "test-common.hpp"

TEST_CASE("source location") {
  std::string content = R""""(Lorem Ipsum
  Dolor
Amid
Set ().$
Try this

)"""";

  CHECK(inja::get_source_location(content, 0).line == 1);
  CHECK(inja::get_source_location(content, 0).column == 1);

  CHECK(inja::get_source_location(content, 10).line == 1);
  CHECK(inja::get_source_location(content, 10).column == 11);

  CHECK(inja::get_source_location(content, 25).line == 4);
  CHECK(inja::get_source_location(content, 25).column == 1);

  CHECK(inja::get_source_location(content, 29).line == 4);
  CHECK(inja::get_source_location(content, 29).column == 5);

  CHECK(inja::get_source_location(content, 43).line == 6);
  CHECK(inja::get_source_location(content, 43).column == 1);
}

TEST_CASE("copy environment") {
  inja::Environment env;
  env.add_callback("double", 1, [](inja::Arguments& args) {
    int number = args.at(0)->get<int>();
    return 2 * number;
  });

  inja::Template t1 = env.parse("{{ double(2) }}");
  env.include_template("tpl", t1);
  std::string test_tpl = "{% include \"tpl\" %}";

  REQUIRE(env.render(test_tpl, inja::json()) == "4");

  inja::Environment copy(env);
  CHECK(copy.render(test_tpl, inja::json()) == "4");

  // overwrite template in source env
  const inja::Template t2 = env.parse("{{ double(4) }}");
  env.include_template("tpl", t2);
  REQUIRE(env.render(test_tpl, inja::json()) == "8");

  // template is unchanged in copy
  CHECK(copy.render(test_tpl, inja::json()) == "4");
}

TEST_CASE("case-insensitive key matching is locale independent") {
  // The ambient C locale must not change how keys fold. std::tolower under a
  // single-byte locale such as cp1251 or cp1252 folds 0xC0-0xDE onto 0xE0-0xFE -
  // precisely the UTF-8 lead-byte range - which would make distinct non-ASCII keys
  // compare equal in a process that happens to have called setlocale.
  const char* const active = std::setlocale(LC_ALL, nullptr);
  const std::string previous = (active != nullptr) ? active : "C";

  // Install whichever high-byte-folding locale this platform happens to have; the
  // assertions below hold under every one of them, including plain "C".
  for (const char* name : {"Russian_Russia.1251", "ru_RU.CP1251", "German_Germany.1252", "de_DE.ISO-8859-1", ""}) {
    if (std::setlocale(LC_ALL, name) != nullptr) {
      break;
    }
  }

  // ASCII still folds
  CHECK(inja::equals_ignore_case("Name", "nAMe"));
  CHECK(inja::equals_ignore_case("ACTOR_0", "actor_0"));
  CHECK_FALSE(inja::equals_ignore_case("name", "names"));

  // 0xC0/0xE0 and 0xDE/0xFE are tolower pairs under Latin-1 and cp1251; ASCII-only
  // folding must keep them distinct
  CHECK_FALSE(inja::equals_ignore_case("\xC0", "\xE0"));
  CHECK_FALSE(inja::equals_ignore_case("key_\xDE", "key_\xFE"));

  std::setlocale(LC_ALL, previous.c_str());
}

TEST_CASE("dotted name array indices") {
  inja::json data;
  data["Actors"] = inja::json::array({inja::json {{"Name", "Bill"}}});

  CHECK(inja::find_by_dotted_name_ignore_case(data, "actors.0.name") != nullptr);
  CHECK(inja::find_by_dotted_name_ignore_case(data, "actors.1.name") == nullptr);
  // Malformed and out-of-range indices resolve to nothing rather than wrapping
  CHECK(inja::find_by_dotted_name_ignore_case(data, "actors.99999999999999999999.name") == nullptr);
  CHECK(inja::find_by_dotted_name_ignore_case(data, "actors.+0.name") == nullptr);
  CHECK(inja::find_by_dotted_name_ignore_case(data, "actors.0x1.name") == nullptr);
  CHECK(inja::find_by_dotted_name_ignore_case(data, "actors..name") == nullptr);
}
