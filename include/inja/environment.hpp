#ifndef INCLUDE_INJA_ENVIRONMENT_HPP_
#define INCLUDE_INJA_ENVIRONMENT_HPP_

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "json.hpp"
#include "config.hpp"
#include "callback_cache.hpp"
#include "function_storage.hpp"
#include "parser.hpp"
#include "renderer.hpp"
#include "template.hpp"
#include "throw.hpp"

namespace inja {

// Forward declaration
class Environment;
void register_array_functions(Environment& env);

/*!
 * \brief Class for changing the configuration.
 */
class Environment {
  FunctionStorage function_storage;
  TemplateStorage template_storage;
  std::vector<RenderErrorInfo> last_render_errors; // Track errors from last render
  std::shared_ptr<CallbackCache> callback_cache_; // Optional callback cache

protected:
  LexerConfig lexer_config;
  ParserConfig parser_config;
  RenderConfig render_config;

  std::filesystem::path input_path;
  std::filesystem::path output_path;

private:
  void init_default_functions() {
    register_array_functions(*this);
  }

public:
  Environment(): Environment("") {}
  
  explicit Environment(const std::filesystem::path& global_path): input_path(global_path), output_path(global_path) {
    init_default_functions();
  }
  
  Environment(const std::filesystem::path& input_path, const std::filesystem::path& output_path): input_path(input_path), output_path(output_path) {
    init_default_functions();
  }

  /// Sets the opener and closer for template statements
  void set_statement(const std::string& open, const std::string& close) {
    lexer_config.statement_open = open;
    lexer_config.statement_open_no_lstrip = open + "+";
    lexer_config.statement_open_force_lstrip = open + "-";
    lexer_config.statement_close = close;
    lexer_config.statement_close_force_rstrip = "-" + close;
    lexer_config.update_open_chars();
  }

  /// Sets the opener for template line statements
  void set_line_statement(const std::string& open) {
    lexer_config.line_statement = open;
    lexer_config.update_open_chars();
  }

  /// Sets the opener and closer for template expressions
  void set_expression(const std::string& open, const std::string& close) {
    lexer_config.expression_open = open;
    lexer_config.expression_open_force_lstrip = open + "-";
    lexer_config.expression_close = close;
    lexer_config.expression_close_force_rstrip = "-" + close;
    lexer_config.update_open_chars();
  }

  /// Sets the opener and closer for template comments
  void set_comment(const std::string& open, const std::string& close) {
    lexer_config.comment_open = open;
    lexer_config.comment_open_force_lstrip = open + "-";
    lexer_config.comment_close = close;
    lexer_config.comment_close_force_rstrip = "-" + close;
    lexer_config.update_open_chars();
  }

  /// Sets whether to remove the first newline after a block
  void set_trim_blocks(bool trim_blocks) {
    lexer_config.trim_blocks = trim_blocks;
  }

  /// Sets whether to strip the spaces and tabs from the start of a line to a block
  void set_lstrip_blocks(bool lstrip_blocks) {
    lexer_config.lstrip_blocks = lstrip_blocks;
  }

  /// Sets the element notation syntax
  void set_search_included_templates_in_files(bool search_in_files) {
    parser_config.search_included_templates_in_files = search_in_files;
  }

  /// Sets whether a missing include will throw an error
  void set_throw_at_missing_includes(bool will_throw) {
    render_config.throw_at_missing_includes = will_throw;
  }

  /// Sets whether we'll automatically perform HTML escape
  void set_html_autoescape(bool will_escape) {
    render_config.html_autoescape = will_escape;
  }

  /*!
   * \brief Sets a callback wrapper for instrumenting callback execution.
   * 
   * When set, all user-defined callbacks will be invoked through this wrapper,
   * allowing external code to measure timing, add tracing spans, etc.
   * Pass an empty/null function to disable wrapping.
   * 
   * Example usage for tracing:
   * @code
   * env.set_callback_wrapper([&ctx](const std::string& name, const std::function<json()>& thunk) {
   *     auto span_id = ctx.StartSpan("decorator:" + name);
   *     auto result = thunk();
   *     ctx.EndSpan(span_id);
   *     return result;
   * });
   * @endcode
   */
  void set_callback_wrapper(const CallbackWrapper& wrapper) {
    render_config.callback_wrapper = wrapper;
  }
  
  /// Clears the callback wrapper (disables instrumentation)
  void clear_callback_wrapper() {
    render_config.callback_wrapper = nullptr;
  }

  /*!
   * \brief Sets an instrumentation callback for receiving internal events.
   *
   * When set, the renderer emits events at key points during template
   * processing (set statements, loops, includes, in-place optimizations, etc.)
   * to provide visibility into internal operations for debugging.
   *
   * @param callback The callback to receive instrumentation events
   */
  void set_instrumentation_callback(const InstrumentationCallback& callback) {
    render_config.instrumentation_callback = callback;
  }

  /// Clears the instrumentation callback
  void clear_instrumentation_callback() {
    render_config.instrumentation_callback = nullptr;
  }

  /*!
   * \brief Enables callback caching with the given configuration.
   *
   * When enabled, callback results will be cached based on function name
   * and arguments, providing significant performance improvements for
   * repeated calls with the same arguments.
   *
   * @param config Cache configuration (TTL, max entries, etc.)
   *
   * Example:
   * @code
   * env.enable_callback_cache(CallbackCacheConfig{
   *     .ttl = std::chrono::seconds(5),
   *     .max_entries = 10000
   * });
   * @endcode
   */
  void enable_callback_cache(const CallbackCacheConfig& config = CallbackCacheConfig{}) {
    callback_cache_ = std::make_shared<CallbackCache>(config);
    render_config.callback_wrapper = callback_cache_->make_caching_wrapper();
  }

  /*!
   * \brief Enables callback caching with a predicate to filter which callbacks are cached.
   *
   * @param config Cache configuration
   * @param predicate Function returning true for callbacks that should be cached
   *
   * Example:
   * @code
   * env.enable_callback_cache(
   *     CallbackCacheConfig{.ttl = std::chrono::seconds(5)},
   *     [](const std::string& name) {
   *         // Don't cache callbacks with side effects or non-deterministic results
   *         return name != "random" && name != "capture_screenshot";
   *     }
   * );
   * @endcode
   */
  void enable_callback_cache(const CallbackCacheConfig& config,
                              CallbackCache::CachePredicate predicate) {
    callback_cache_ = std::make_shared<CallbackCache>(config);
    callback_cache_->set_cache_predicate(std::move(predicate));
    render_config.callback_wrapper = callback_cache_->make_caching_wrapper();
  }

  /*!
   * \brief Enables callback caching with an inner wrapper for chained instrumentation.
   *
   * This allows combining caching with other instrumentation like tracing.
   * The inner wrapper is called only on cache misses.
   *
   * @param config Cache configuration
   * @param inner_wrapper Wrapper to call on cache misses (e.g., for tracing)
   * @param predicate Optional predicate to filter which callbacks are cached
   *
   * Example:
   * @code
   * env.enable_callback_cache_with_wrapper(
   *     CallbackCacheConfig{.ttl = std::chrono::seconds(5)},
   *     [&ctx](const std::string& name, const Arguments& args,
   *            const std::function<json()>& thunk) {
   *         auto span = ctx.StartSpan("decorator:" + name);
   *         auto result = thunk();
   *         ctx.EndSpan(span);
   *         return result;
   *     }
   * );
   * @endcode
   */
  void enable_callback_cache_with_wrapper(const CallbackCacheConfig& config,
                                           const CallbackWrapper& inner_wrapper,
                                           CallbackCache::CachePredicate predicate = nullptr) {
    callback_cache_ = std::make_shared<CallbackCache>(config);
    if (predicate) {
      callback_cache_->set_cache_predicate(std::move(predicate));
    }
    render_config.callback_wrapper = callback_cache_->make_caching_wrapper_with_inner(inner_wrapper);
  }

  /*!
   * \brief Sets an external callback cache instance.
   *
   * This allows sharing a cache between multiple Environment instances (e.g., when
   * copying a PromptEngine for different ContextEngine renders within the same dialogue turn).
   *
   * @param cache The external cache instance to use
   * @param predicate Optional predicate to filter which callbacks are cached
   *
   * Example:
   * @code
   * auto shared_cache = std::make_shared<CallbackCache>(CallbackCacheConfig{
   *     .ttl = std::chrono::seconds(5),
   *     .max_entries = 10000
   * });
   *
   * // Both environments share the same cache
   * env1.set_callback_cache(shared_cache, my_predicate);
   * env2.set_callback_cache(shared_cache, my_predicate);
   * @endcode
   */
  void set_callback_cache(std::shared_ptr<CallbackCache> cache,
                          CallbackCache::CachePredicate predicate = nullptr) {
    callback_cache_ = cache;
    if (predicate && cache) {
      cache->set_cache_predicate(std::move(predicate));
    }
    if (cache) {
      render_config.callback_wrapper = cache->make_caching_wrapper();
    } else {
      render_config.callback_wrapper = nullptr;
    }
  }

  /*!
   * \brief Disables callback caching.
   *
   * Clears the cache and removes the caching wrapper.
   * Note: If you had a callback wrapper set before enabling caching, you'll need to re-set it.
   */
  void disable_callback_cache() {
    callback_cache_.reset();
    render_config.callback_wrapper = nullptr;
  }

  /*!
   * \brief Gets the callback cache instance.
   *
   * @return Shared pointer to the cache, or nullptr if caching is not enabled
   */
  std::shared_ptr<CallbackCache> get_callback_cache() const {
    return callback_cache_;
  }

  /*!
   * \brief Clears all entries in the callback cache.
   *
   * Does nothing if caching is not enabled.
   */
  void clear_callback_cache() {
    if (callback_cache_) {
      callback_cache_->clear();
    }
  }

  /*!
   * \brief Invalidates cached entries for a specific callback.
   *
   * @param function_name The callback to invalidate
   * @return Number of entries removed
   */
  size_t invalidate_callback_cache(const std::string& function_name) {
    if (callback_cache_) {
      return callback_cache_->invalidate(function_name);
    }
    return 0;
  }

  Template parse(std::string_view input) {
    Parser parser(parser_config, lexer_config, template_storage, function_storage);
    return parser.parse(input, input_path);
  }

  Template parse_template(const std::filesystem::path& filename) {
    Parser parser(parser_config, lexer_config, template_storage, function_storage);
    auto result = Template(Parser::load_file(input_path / filename));
    parser.parse_into_template(result, (input_path / filename).string());
    return result;
  }

  Template parse_file(const std::filesystem::path& filename) {
    return parse_template(filename);
  }

  std::string render(std::string_view input, const json& data) {
    return render(parse(input), data);
  }

  std::string render(const Template& tmpl, const json& data) {
    std::stringstream os;
    render_to(os, tmpl, data);
    return os.str();
  }

  std::string render_file(const std::filesystem::path& filename, const json& data) {
    return render(parse_template(filename), data);
  }

  std::string render_file_with_json_file(const std::filesystem::path& filename, const std::string& filename_data) {
    const json data = load_json(filename_data);
    return render_file(filename, data);
  }

  void write(const std::filesystem::path& filename, const json& data, const std::string& filename_out) {
    std::ofstream file(output_path / filename_out);
    file << render_file(filename, data);
    file.close();
  }

  void write(const Template& temp, const json& data, const std::string& filename_out) {
    std::ofstream file(output_path / filename_out);
    file << render(temp, data);
    file.close();
  }

  void write_with_json_file(const std::filesystem::path& filename, const std::string& filename_data, const std::string& filename_out) {
    const json data = load_json(filename_data);
    write(filename, data, filename_out);
  }

  void write_with_json_file(const Template& temp, const std::string& filename_data, const std::string& filename_out) {
    const json data = load_json(filename_data);
    write(temp, data, filename_out);
  }

  std::ostream& render_to(std::ostream& os, const Template& tmpl, const json& data) {
    Renderer renderer(render_config, template_storage, function_storage);
    renderer.render_to(os, tmpl, data);
    last_render_errors = renderer.get_render_errors();
    return os;
  }

  std::ostream& render_to(std::ostream& os, const std::string_view input, const json& data) {
    return render_to(os, parse(input), data);
  }
  
  /// Gets the errors from the last render operation (only populated in graceful_errors mode)
  const std::vector<RenderErrorInfo>& get_last_render_errors() const {
    return last_render_errors;
  }
  
  /// Clears the stored render errors
  void clear_render_errors() {
    last_render_errors.clear();
  }
  
  /// Sets whether to use graceful error handling (missing variables/functions render as original text)
  void set_graceful_errors(bool graceful) {
    parser_config.graceful_errors = graceful;
    render_config.graceful_errors = graceful;
  }

  std::string load_file(const std::string& filename) {
    const Parser parser(parser_config, lexer_config, template_storage, function_storage);
    return Parser::load_file(input_path / filename);
  }

  json load_json(const std::string& filename) {
    std::ifstream file;
    file.open(input_path / filename);
    if (file.fail()) {
      INJA_THROW(FileError("failed accessing file at '" + (input_path / filename).string() + "'"));
    }

    return json::parse(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }

  /*!
  @brief Adds a variadic callback
  */
  void add_callback(const std::string& name, const CallbackFunction& callback) {
    add_callback(name, -1, callback);
  }

  /*!
  @brief Adds a variadic void callback
  */
  void add_void_callback(const std::string& name, const VoidCallbackFunction& callback) {
    add_void_callback(name, -1, callback);
  }

  /*!
  @brief Adds a callback with given number or arguments
  */
  void add_callback(const std::string& name, int num_args, const CallbackFunction& callback) {
    function_storage.add_callback(name, num_args, callback);
  }

  /*!
  @brief Adds a callback with an optional in-place mutation optimization.

  The in-place callback is used when the renderer detects a self-assignment pattern:
    {% set x = func(x, ...) %}

  In this case, instead of copying x and then assigning the result back,
  the in-place callback mutates x directly, avoiding the copy.
  */
  void add_callback(const std::string& name, int num_args, const CallbackFunction& callback,
                    const InPlaceCallbackFunction& inplace_callback) {
    function_storage.add_callback(name, num_args, callback, inplace_callback);
  }

  /*!
  @brief Adds a void callback with given number or arguments
  */
  void add_void_callback(const std::string& name, int num_args, const VoidCallbackFunction& callback) {
    function_storage.add_callback(name, num_args, [callback](Arguments& args) {
      callback(args);
      return json();
    });
  }

  /** Includes a template with a given name into the environment.
   * Then, a template can be rendered in another template using the
   * include "<name>" syntax.
   */
  void include_template(const std::string& name, const Template& tmpl) {
    template_storage[name] = tmpl;
  }

  /*!
  @brief Sets a function that is called when an included file is not found
  */
  void set_include_callback(const std::function<Template(const std::filesystem::path&, const std::string&)>& callback) {
    parser_config.include_callback = callback;
  }
};

/*!
@brief render with default settings to a string
*/
inline std::string render(std::string_view input, const json& data) {
  return Environment().render(input, data);
}

/*!
@brief render with default settings to the given output stream
*/
inline void render_to(std::ostream& os, std::string_view input, const json& data) {
  Environment env;
  env.render_to(os, env.parse(input), data);
}

} // namespace inja

// Include array functions implementation after Environment is fully defined
#include "array_functions.hpp"

#endif // INCLUDE_INJA_ENVIRONMENT_HPP_
