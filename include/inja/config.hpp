#ifndef INCLUDE_INJA_CONFIG_HPP_
#define INCLUDE_INJA_CONFIG_HPP_

#include <filesystem>
#include <functional>
#include <string>

#include "template.hpp"
#include "json.hpp"

namespace inja {

/*!
 * \brief Type for callback wrapper function used for tracing/instrumentation.
 * 
 * The wrapper receives the function name, the arguments passed to the callback,
 * and a thunk that executes the actual callback.
 * This allows external code to wrap callback execution with timing, tracing, 
 * argument logging, return value inspection, etc.
 * 
 * Usage: wrapper("function_name", args, [&]() { return actual_callback(args); })
 */
using CallbackWrapper = std::function<json(const std::string& function_name, const Arguments& args, const std::function<json()>& callback_thunk)>;

/*!
 * \brief Class for lexer configuration.
 */
struct LexerConfig {
  std::string statement_open {"{%"};
  std::string statement_open_no_lstrip {"{%+"};
  std::string statement_open_force_lstrip {"{%-"};
  std::string statement_close {"%}"};
  std::string statement_close_force_rstrip {"-%}"};
  std::string line_statement {"##"};
  std::string expression_open {"{{"};
  std::string expression_open_force_lstrip {"{{-"};
  std::string expression_close {"}}"};
  std::string expression_close_force_rstrip {"-}}"};
  std::string comment_open {"{#"};
  std::string comment_open_force_lstrip {"{#-"};
  std::string comment_close {"#}"};
  std::string comment_close_force_rstrip {"-#}"};
  std::string open_chars {"#{"};

  bool trim_blocks {false};
  bool lstrip_blocks {false};

  void update_open_chars() {
    open_chars = "";
    if (open_chars.find(line_statement[0]) == std::string::npos) {
      open_chars += line_statement[0];
    }
    if (open_chars.find(statement_open[0]) == std::string::npos) {
      open_chars += statement_open[0];
    }
    if (open_chars.find(statement_open_no_lstrip[0]) == std::string::npos) {
      open_chars += statement_open_no_lstrip[0];
    }
    if (open_chars.find(statement_open_force_lstrip[0]) == std::string::npos) {
      open_chars += statement_open_force_lstrip[0];
    }
    if (open_chars.find(expression_open[0]) == std::string::npos) {
      open_chars += expression_open[0];
    }
    if (open_chars.find(expression_open_force_lstrip[0]) == std::string::npos) {
      open_chars += expression_open_force_lstrip[0];
    }
    if (open_chars.find(comment_open[0]) == std::string::npos) {
      open_chars += comment_open[0];
    }
    if (open_chars.find(comment_open_force_lstrip[0]) == std::string::npos) {
      open_chars += comment_open_force_lstrip[0];
    }
  }
};

/*!
 * \brief Class for parser configuration.
 */
struct ParserConfig {
  bool search_included_templates_in_files {true};
  bool graceful_errors {false}; // If true, allow unknown functions at parse time

  std::function<Template(const std::filesystem::path&, const std::string&)> include_callback;
};

/*!
 * \brief Class for render configuration.
 */
struct RenderConfig {
  bool throw_at_missing_includes {true};
  bool html_autoescape {false};
  bool graceful_errors {false}; // If true, missing variables/functions render as original template text
  
  /*!
   * \brief Optional callback wrapper for instrumenting callback execution.
   * 
   * When set, all user-defined callbacks will be invoked through this wrapper,
   * allowing external code to measure timing, add tracing spans, etc.
   * 
   * The wrapper receives the callback function name and a thunk that executes
   * the actual callback. Example usage for tracing:
   * 
   * render_config.callback_wrapper = [&](const std::string& name, const std::function<json()>& thunk) {
   *     auto span = trace_context.StartSpan("decorator:" + name);
   *     auto result = thunk();
   *     trace_context.EndSpan(span);
   *     return result;
   * };
   */
  CallbackWrapper callback_wrapper;
};

} // namespace inja

#endif // INCLUDE_INJA_CONFIG_HPP_
