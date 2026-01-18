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
 * \brief Event types for Inja instrumentation.
 *
 * These events are emitted during template rendering to provide visibility
 * into internal operations for debugging and performance analysis.
 */
enum class InstrumentationEvent {
  // Template rendering lifecycle
  RenderStart,           // Template rendering started
  RenderEnd,             // Template rendering completed

  // Set statement events
  SetStatementStart,     // Beginning of set statement evaluation
  SetStatementEnd,       // End of set statement evaluation

  // In-place optimization events
  InplaceOptUsed,        // In-place optimization was successfully used
  InplaceOptSkipped,     // In-place optimization was skipped (with reason)

  // Expression evaluation
  ExpressionEvalStart,   // Beginning of expression evaluation
  ExpressionEvalEnd,     // End of expression evaluation

  // Loop events
  ForLoopStart,          // Beginning of for loop
  ForLoopIteration,      // Each iteration of a for loop
  ForLoopEnd,            // End of for loop

  // Include/block events
  IncludeStart,          // Including another template
  IncludeEnd,            // Finished including template
};

/*!
 * \brief Data associated with instrumentation events.
 */
struct InstrumentationData {
  InstrumentationEvent event;
  std::string name;              // Variable name, template name, function name, etc.
  std::string detail;            // Additional detail (e.g., skip reason, loop count)
  size_t count {0};              // Numeric data (e.g., iteration count, array size)

  InstrumentationData(InstrumentationEvent e) : event(e) {}
  InstrumentationData(InstrumentationEvent e, const std::string& n) : event(e), name(n) {}
  InstrumentationData(InstrumentationEvent e, const std::string& n, const std::string& d)
      : event(e), name(n), detail(d) {}
  InstrumentationData(InstrumentationEvent e, const std::string& n, const std::string& d, size_t c)
      : event(e), name(n), detail(d), count(c) {}
};

/*!
 * \brief Callback type for receiving instrumentation events.
 *
 * The callback receives an InstrumentationData struct with event details.
 * This is called synchronously during rendering, so implementations should
 * be fast to avoid impacting render performance.
 */
using InstrumentationCallback = std::function<void(const InstrumentationData& data)>;

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

  /*!
   * \brief Optional instrumentation callback for receiving internal events.
   *
   * When set, the renderer emits events at key points during template
   * processing (set statements, loops, includes, in-place optimizations, etc.)
   * to provide visibility into internal operations for debugging.
   */
  InstrumentationCallback instrumentation_callback;
};

} // namespace inja

#endif // INCLUDE_INJA_CONFIG_HPP_
