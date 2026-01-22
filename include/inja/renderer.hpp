#ifndef INCLUDE_INJA_RENDERER_HPP_
#define INCLUDE_INJA_RENDERER_HPP_

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numeric>
#include <ostream>
#include <sstream>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "exceptions.hpp"
#include "function_storage.hpp"
#include "node.hpp"
#include "template.hpp"
#include "throw.hpp"
#include "utils.hpp"

namespace inja {

/*!
@brief Helper struct for tracking not found variables/functions
*/
struct NotFoundInfo {
  std::string name;
  const AstNode* node;
  
  NotFoundInfo(const std::string& name, const AstNode* node) : name(name), node(node) {}
};

/*!
@brief Escapes HTML
*/
inline std::string htmlescape(const std::string& data) {
  std::string buffer;
  buffer.reserve(static_cast<size_t>(1.1 * data.size()));
  for (size_t pos = 0; pos != data.size(); ++pos) {
    switch (data[pos]) {
      case '&':  buffer.append("&amp;");       break;
      case '\"': buffer.append("&quot;");      break;
      case '\'': buffer.append("&apos;");      break;
      case '<':  buffer.append("&lt;");        break;
      case '>':  buffer.append("&gt;");        break;
      default:   buffer.append(&data[pos], 1); break;
    }
  }
  return buffer;
}

/*!
 * \brief Class for rendering a Template with data.
 */
class Renderer : public NodeVisitor {
  using Op = FunctionStorage::Operation;

  const RenderConfig config;
  const TemplateStorage& template_storage;
  const FunctionStorage& function_storage;

  const Template* current_template;
  size_t current_level {0};
  std::vector<const Template*> template_stack;
  std::vector<const BlockStatementNode*> block_statement_stack;

  const json* data_input;
  std::ostream* output_stream;

  json additional_data;
  json* current_loop_data = &additional_data["loop"];

  std::vector<std::shared_ptr<json>> data_tmp_stack;
  std::stack<const json*> data_eval_stack;
  std::stack<NotFoundInfo> not_found_stack; // Can hold DataNode or FunctionNode for error reporting

  bool break_rendering {false};

  std::vector<RenderErrorInfo> render_errors; // Track errors in graceful mode (per-instance)

  static bool truthy(const json* data) {
    // In graceful error mode, data can be nullptr for missing variables
    if (!data) {
      return false;
    }
    if (data->is_boolean()) {
      return data->get<bool>();
    } else if (data->is_number()) {
      return (*data != 0);
    } else if (data->is_null()) {
      return false;
    }
    return !data->empty();
  }

  void emit_event(InstrumentationEvent event) {
    if (config.instrumentation_callback) {
      config.instrumentation_callback(InstrumentationData(event));
    }
  }

  void emit_event(InstrumentationEvent event, const std::string& name) {
    if (config.instrumentation_callback) {
      config.instrumentation_callback(InstrumentationData(event, name));
    }
  }

  void emit_event(InstrumentationEvent event, const std::string& name, const std::string& detail) {
    if (config.instrumentation_callback) {
      config.instrumentation_callback(InstrumentationData(event, name, detail));
    }
  }

  void emit_event(InstrumentationEvent event, const std::string& name, const std::string& detail, size_t count) {
    if (config.instrumentation_callback) {
      config.instrumentation_callback(InstrumentationData(event, name, detail, count));
    }
  }

  void print_data(const std::shared_ptr<json>& value) {
    if (value->is_string()) {
      if (config.html_autoescape) {
        *output_stream << htmlescape(value->get_ref<const json::string_t&>());
      } else {
        *output_stream << value->get_ref<const json::string_t&>();
      }
    } else if (value->is_number_unsigned()) {
      *output_stream << value->get<const json::number_unsigned_t>();
    } else if (value->is_number_integer()) {
      *output_stream << value->get<const json::number_integer_t>();
    } else if (value->is_null()) {
    } else {
      *output_stream << value->dump();
    }
  }

  const std::shared_ptr<json> eval_expression_list(const ExpressionListNode& expression_list) {
    if (!expression_list.root) {
      std::string original_text;
      if (config.graceful_errors && expression_list.length > 0) {
        original_text = current_template->content.substr(expression_list.pos, expression_list.length);
      }
      throw_renderer_error("empty expression", expression_list, original_text);
      return nullptr;
    }

    expression_list.root->accept(*this);

    if (data_eval_stack.empty()) {
      std::string original_text;
      if (config.graceful_errors && expression_list.length > 0) {
        original_text = current_template->content.substr(expression_list.pos, expression_list.length);
      }
      throw_renderer_error("empty expression", expression_list, original_text);
      return nullptr;
    } else if (data_eval_stack.size() != 1) {
      std::string original_text;
      if (config.graceful_errors && expression_list.length > 0) {
        original_text = current_template->content.substr(expression_list.pos, expression_list.length);
      }
      throw_renderer_error("malformed expression", expression_list, original_text);
      return nullptr;
    }

    const auto result = data_eval_stack.top();
    data_eval_stack.pop();

    if (result == nullptr) {
      if (not_found_stack.empty()) {
        std::string original_text;
        if (config.graceful_errors && expression_list.length > 0) {
          original_text = current_template->content.substr(expression_list.pos, expression_list.length);
        }
        throw_renderer_error("expression could not be evaluated", expression_list, original_text);
        return nullptr;
      }

      const auto not_found = not_found_stack.top();
      not_found_stack.pop();

      std::string original_text;
      if (config.graceful_errors && expression_list.length > 0) {
        original_text = current_template->content.substr(expression_list.pos, expression_list.length);
      }
      throw_renderer_error("variable '" + not_found.name + "' not found", *not_found.node, original_text);
      return nullptr;
    }
    return std::make_shared<json>(*result);
  }

  void throw_renderer_error(const std::string& message, const AstNode& node, const std::string& original_text = "") {
    const SourceLocation loc = get_source_location(current_template->content, node.pos);
    if (config.graceful_errors) {
      render_errors.emplace_back(message, loc, original_text);
    } else {
      INJA_THROW(RenderError(message, loc));
    }
  }

  void make_result(const json&& result) {
    auto result_ptr = std::make_shared<json>(result);
    data_tmp_stack.push_back(result_ptr);
    data_eval_stack.push(result_ptr.get());
  }

  template <size_t N, size_t N_start = 0, bool throw_not_found = true> std::array<const json*, N> get_arguments(const FunctionNode& node) {
    if (node.arguments.size() < N_start + N) {
      throw_renderer_error("function needs " + std::to_string(N_start + N) + " variables, but has only found " + std::to_string(node.arguments.size()), node);
    }

    for (size_t i = N_start; i < N_start + N; i += 1) {
      node.arguments[i]->accept(*this);
    }

    if (data_eval_stack.size() < N) {
      throw_renderer_error("function needs " + std::to_string(N) + " variables, but has only found " + std::to_string(data_eval_stack.size()), node);
    }

    std::array<const json*, N> result;
    for (size_t i = 0; i < N; i += 1) {
      result[N - i - 1] = data_eval_stack.top();
      data_eval_stack.pop();

      if (!result[N - i - 1]) {
        const auto not_found = not_found_stack.top();
        not_found_stack.pop();

        if (throw_not_found) {
          throw_renderer_error("variable '" + not_found.name + "' not found", *not_found.node);
        }
        
        // In graceful error mode, provide a safe default to prevent null pointer dereferences
        // But only when the caller expects to throw - if throw_not_found is false, the caller
        // explicitly wants to handle nullptr (e.g., the Default filter)
        if (config.graceful_errors && throw_not_found) {
          static const json empty_json;
          result[N - i - 1] = &empty_json;
        }
        // If throw_not_found is false, leave as nullptr so caller can detect missing value
      }
    }
    return result;
  }

  template <bool throw_not_found = true> Arguments get_argument_vector(const FunctionNode& node) {
    const size_t N = node.arguments.size();
    for (const auto& a : node.arguments) {
      a->accept(*this);
    }

    if (data_eval_stack.size() < N) {
      throw_renderer_error("function needs " + std::to_string(N) + " variables, but has only found " + std::to_string(data_eval_stack.size()), node);
    }

    Arguments result {N};
    for (size_t i = 0; i < N; i += 1) {
      result[N - i - 1] = data_eval_stack.top();
      data_eval_stack.pop();

      if (!result[N - i - 1]) {
        const auto not_found = not_found_stack.top();
        not_found_stack.pop();

        if (throw_not_found) {
          throw_renderer_error("variable '" + not_found.name + "' not found", *not_found.node);
        }
        
        // In graceful error mode, provide a safe default to prevent null pointer dereferences
        // But only when the caller expects to throw - if throw_not_found is false, the caller
        // explicitly wants to handle nullptr (e.g., the Default filter)
        if (config.graceful_errors && throw_not_found) {
          static const json empty_json;
          result[N - i - 1] = &empty_json;
        }
        // If throw_not_found is false, leave as nullptr so caller can detect missing value
      }
    }
    return result;
  }

  void visit(const BlockNode& node) override {
    for (const auto& n : node.nodes) {
      n->accept(*this);

      if (break_rendering) {
        break;
      }
    }
  }

  void visit(const TextNode& node) override {
    output_stream->write(current_template->content.c_str() + node.pos, node.length);
  }

  void visit(const ExpressionNode&) override {}

  void visit(const LiteralNode& node) override {
    data_eval_stack.push(&node.value);
  }

  void visit(const DataNode& node) override {
    if (additional_data.contains(node.ptr)) {
      data_eval_stack.push(&(additional_data[node.ptr]));
    } else if (data_input->contains(node.ptr)) {
      data_eval_stack.push(&(*data_input)[node.ptr]);
    } else {
      // Try to evaluate as a no-argument callback
      const auto function_data = function_storage.find_function(node.name, 0);
      if (function_data.operation == FunctionStorage::Operation::Callback) {
        Arguments empty_args {};
        // If a callback wrapper is set (for tracing/instrumentation), use it
        std::shared_ptr<json> value;
        if (config.callback_wrapper) {
          value = std::make_shared<json>(config.callback_wrapper(static_cast<std::string>(node.name), empty_args, [&]() {
            return function_data.callback(empty_args);
          }));
        } else {
          value = std::make_shared<json>(function_data.callback(empty_args));
        }
        data_tmp_stack.push_back(value);
        data_eval_stack.push(value.get());
      } else {
        data_eval_stack.push(nullptr);
        not_found_stack.emplace(static_cast<std::string>(node.name), &node);
      }
    }
  }

  // Helper macro for graceful error handling in operations
  #define INJA_OP_TRY_BEGIN try {
  #define INJA_OP_TRY_END_GRACEFUL(op_name) \
    } catch (const std::exception& e) { \
      if (config.graceful_errors) { \
        data_eval_stack.push(nullptr); \
        not_found_stack.emplace(op_name, &node); \
      } else { \
        throw_renderer_error(std::string("operation '") + op_name + "' failed: " + e.what(), node); \
      } \
    } catch (...) { \
      if (config.graceful_errors) { \
        data_eval_stack.push(nullptr); \
        not_found_stack.emplace(op_name, &node); \
      } else { \
        throw_renderer_error(std::string("operation '") + op_name + "' failed with unknown exception", node); \
      } \
    }

  void visit(const FunctionNode& node) override {
    switch (node.operation) {
    case Op::Not: {
      const auto args = get_arguments<1>(node);
      make_result(!truthy(args[0]));
    } break;
    case Op::And: {
      make_result(truthy(get_arguments<1, 0>(node)[0]) && truthy(get_arguments<1, 1>(node)[0]));
    } break;
    case Op::Or: {
      make_result(truthy(get_arguments<1, 0>(node)[0]) || truthy(get_arguments<1, 1>(node)[0]));
    } break;
    case Op::In: {
      const auto args = get_arguments<2>(node);
      make_result(std::find(args[1]->begin(), args[1]->end(), *args[0]) != args[1]->end());
    } break;
    case Op::Equal: {
      const auto args = get_arguments<2>(node);
      make_result(*args[0] == *args[1]);
    } break;
    case Op::NotEqual: {
      const auto args = get_arguments<2>(node);
      make_result(*args[0] != *args[1]);
    } break;
    case Op::Greater: {
      const auto args = get_arguments<2>(node);
      make_result(*args[0] > *args[1]);
    } break;
    case Op::GreaterEqual: {
      const auto args = get_arguments<2>(node);
      make_result(*args[0] >= *args[1]);
    } break;
    case Op::Less: {
      const auto args = get_arguments<2>(node);
      make_result(*args[0] < *args[1]);
    } break;
    case Op::LessEqual: {
      const auto args = get_arguments<2>(node);
      make_result(*args[0] <= *args[1]);
    } break;
    case Op::Add: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        if (args[0]->is_string() && args[1]->is_string()) {
          make_result(args[0]->get_ref<const json::string_t&>() + args[1]->get_ref<const json::string_t&>());
        } else if (args[0]->is_number_integer() && args[1]->is_number_integer()) {
          make_result(args[0]->get<const json::number_integer_t>() + args[1]->get<const json::number_integer_t>());
        } else {
          make_result(args[0]->get<const json::number_float_t>() + args[1]->get<const json::number_float_t>());
        }
      INJA_OP_TRY_END_GRACEFUL("add")
    } break;
    case Op::Subtract: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        if (args[0]->is_number_integer() && args[1]->is_number_integer()) {
          make_result(args[0]->get<const json::number_integer_t>() - args[1]->get<const json::number_integer_t>());
        } else {
          make_result(args[0]->get<const json::number_float_t>() - args[1]->get<const json::number_float_t>());
        }
      INJA_OP_TRY_END_GRACEFUL("subtract")
    } break;
    case Op::Multiplication: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        if (args[0]->is_number_integer() && args[1]->is_number_integer()) {
          make_result(args[0]->get<const json::number_integer_t>() * args[1]->get<const json::number_integer_t>());
        } else {
          make_result(args[0]->get<const json::number_float_t>() * args[1]->get<const json::number_float_t>());
        }
      INJA_OP_TRY_END_GRACEFUL("multiply")
    } break;
    case Op::Division: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        if (args[1]->get<const json::number_float_t>() == 0) {
          throw_renderer_error("division by zero", node);
        }
        make_result(args[0]->get<const json::number_float_t>() / args[1]->get<const json::number_float_t>());
      INJA_OP_TRY_END_GRACEFUL("division")
    } break;
    case Op::Power: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        if (args[0]->is_number_integer() && args[1]->get<const json::number_integer_t>() >= 0) {
          const auto result = static_cast<json::number_integer_t>(std::pow(args[0]->get<const json::number_integer_t>(), args[1]->get<const json::number_integer_t>()));
          make_result(result);
        } else {
          const auto result = std::pow(args[0]->get<const json::number_float_t>(), args[1]->get<const json::number_integer_t>());
          make_result(result);
        }
      INJA_OP_TRY_END_GRACEFUL("power")
    } break;
    case Op::Modulo: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        make_result(args[0]->get<const json::number_integer_t>() % args[1]->get<const json::number_integer_t>());
      INJA_OP_TRY_END_GRACEFUL("modulo")
    } break;
    case Op::AtId: {
      const auto container = get_arguments<1, 0, false>(node)[0];
      node.arguments[1]->accept(*this);
      if (not_found_stack.empty()) {
        throw_renderer_error("could not find element with given name", node);
      }
      const auto not_found = not_found_stack.top();
      not_found_stack.pop();
      data_eval_stack.pop();
      
      // Safe access with graceful error handling
      try {
        if (container && container->contains(not_found.name)) {
          data_eval_stack.push(&container->at(not_found.name));
        } else {
          // Member not found
          if (config.graceful_errors) {
            data_eval_stack.push(nullptr);
            not_found_stack.push(not_found);
          } else {
            throw_renderer_error("member '" + not_found.name + "' not found in container", node);
          }
        }
      } catch (const std::exception&) {
        if (config.graceful_errors) {
          data_eval_stack.push(nullptr);
          not_found_stack.push(not_found);
        } else {
          throw;
        }
      }
    } break;
    case Op::At: {
      const auto args = get_arguments<2>(node);
      try {
        if (args[0]->is_object()) {
          const auto key = args[1]->get<std::string>();
          if (args[0]->contains(key)) {
            data_eval_stack.push(&args[0]->at(key));
          } else {
            if (config.graceful_errors) {
              data_eval_stack.push(nullptr);
              not_found_stack.emplace(key, &node);
            } else {
              throw_renderer_error("key '" + key + "' not found in object", node);
            }
          }
        } else if (args[0]->is_array()) {
          const auto index = args[1]->get<int>();
          if (index >= 0 && static_cast<size_t>(index) < args[0]->size()) {
            data_eval_stack.push(&args[0]->at(index));
          } else {
            if (config.graceful_errors) {
              data_eval_stack.push(nullptr);
              not_found_stack.emplace("index[" + std::to_string(index) + "]", &node);
            } else {
              throw_renderer_error("index " + std::to_string(index) + " out of bounds", node);
            }
          }
        } else {
          if (config.graceful_errors) {
            data_eval_stack.push(nullptr);
            not_found_stack.emplace("at", &node);
          } else {
            throw_renderer_error("cannot access element on non-container type", node);
          }
        }
      } catch (const std::exception&) {
        if (config.graceful_errors) {
          data_eval_stack.push(nullptr);
          not_found_stack.emplace("at", &node);
        } else {
          throw;
        }
      }
    } break;
    case Op::Capitalize: {
      INJA_OP_TRY_BEGIN
        auto result = get_arguments<1>(node)[0]->get<json::string_t>();
        result[0] = static_cast<char>(::toupper(result[0]));
        std::transform(result.begin() + 1, result.end(), result.begin() + 1, [](char c) { return static_cast<char>(::tolower(c)); });
        make_result(std::move(result));
      INJA_OP_TRY_END_GRACEFUL("capitalize")
    } break;
    case Op::Default: {
      const auto test_arg = get_arguments<1, 0, false>(node)[0];
      data_eval_stack.push((test_arg != nullptr) ? test_arg : get_arguments<1, 1>(node)[0]);
    } break;
    case Op::DivisibleBy: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        const auto divisor = args[1]->get<const json::number_integer_t>();
        make_result((divisor != 0) && (args[0]->get<const json::number_integer_t>() % divisor == 0));
      INJA_OP_TRY_END_GRACEFUL("divisibleBy")
    } break;
    case Op::Even: {
      INJA_OP_TRY_BEGIN
        make_result(get_arguments<1>(node)[0]->get<const json::number_integer_t>() % 2 == 0);
      INJA_OP_TRY_END_GRACEFUL("even")
    } break;
    case Op::Exists: {
      INJA_OP_TRY_BEGIN
        auto&& name = get_arguments<1>(node)[0]->get_ref<const json::string_t&>();
        make_result(data_input->contains(json::json_pointer(DataNode::convert_dot_to_ptr(name))));
      INJA_OP_TRY_END_GRACEFUL("exists")
    } break;
    case Op::ExistsInObject: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        auto&& name = args[1]->get_ref<const json::string_t&>();
        make_result(args[0]->find(name) != args[0]->end());
      INJA_OP_TRY_END_GRACEFUL("existsIn")
    } break;
    case Op::First: {
      INJA_OP_TRY_BEGIN
        const auto arr = get_arguments<1>(node)[0];
        if (arr->empty()) {
          if (config.graceful_errors) {
            data_eval_stack.push(nullptr);
            not_found_stack.emplace("first", &node);
          } else {
            throw_renderer_error("cannot get first element of empty array", node);
          }
        } else {
          const auto result = &arr->front();
          data_eval_stack.push(result);
        }
      INJA_OP_TRY_END_GRACEFUL("first")
    } break;
    case Op::Float: {
      INJA_OP_TRY_BEGIN
        make_result(std::stod(get_arguments<1>(node)[0]->get_ref<const json::string_t&>()));
      INJA_OP_TRY_END_GRACEFUL("float")
    } break;
    case Op::Int: {
      INJA_OP_TRY_BEGIN
        make_result(std::stoi(get_arguments<1>(node)[0]->get_ref<const json::string_t&>()));
      INJA_OP_TRY_END_GRACEFUL("int")
    } break;
    case Op::Last: {
      INJA_OP_TRY_BEGIN
        const auto arr = get_arguments<1>(node)[0];
        if (arr->empty()) {
          if (config.graceful_errors) {
            data_eval_stack.push(nullptr);
            not_found_stack.emplace("last", &node);
          } else {
            throw_renderer_error("cannot get last element of empty array", node);
          }
        } else {
          const auto result = &arr->back();
          data_eval_stack.push(result);
        }
      INJA_OP_TRY_END_GRACEFUL("last")
    } break;
    case Op::Length: {
      INJA_OP_TRY_BEGIN
        const auto val = get_arguments<1>(node)[0];
        if (val->is_string()) {
          make_result(val->get_ref<const json::string_t&>().length());
        } else {
          make_result(val->size());
        }
      INJA_OP_TRY_END_GRACEFUL("length")
    } break;
    case Op::Lower: {
      INJA_OP_TRY_BEGIN
        auto result = get_arguments<1>(node)[0]->get<json::string_t>();
        std::transform(result.begin(), result.end(), result.begin(), [](char c) { return static_cast<char>(::tolower(c)); });
        make_result(std::move(result));
      INJA_OP_TRY_END_GRACEFUL("lower")
    } break;
    case Op::Max: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<1>(node);
        const auto result = std::max_element(args[0]->begin(), args[0]->end());
        data_eval_stack.push(&(*result));
      INJA_OP_TRY_END_GRACEFUL("max")
    } break;
    case Op::Min: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<1>(node);
        const auto result = std::min_element(args[0]->begin(), args[0]->end());
        data_eval_stack.push(&(*result));
      INJA_OP_TRY_END_GRACEFUL("min")
    } break;
    case Op::Odd: {
      INJA_OP_TRY_BEGIN
        make_result(get_arguments<1>(node)[0]->get<const json::number_integer_t>() % 2 != 0);
      INJA_OP_TRY_END_GRACEFUL("odd")
    } break;
    case Op::Range: {
      INJA_OP_TRY_BEGIN
        std::vector<int> result(get_arguments<1>(node)[0]->get<const json::number_integer_t>());
        std::iota(result.begin(), result.end(), 0);
        make_result(std::move(result));
      INJA_OP_TRY_END_GRACEFUL("range")
    } break;
    case Op::Replace: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<3>(node);
        auto result = args[0]->get<std::string>();
        replace_substring(result, args[1]->get<std::string>(), args[2]->get<std::string>());
        make_result(std::move(result));
      INJA_OP_TRY_END_GRACEFUL("replace")
    } break;
    case Op::Round: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        const auto precision = args[1]->get<const json::number_integer_t>();
        const double result = std::round(args[0]->get<const json::number_float_t>() * std::pow(10.0, precision)) / std::pow(10.0, precision);
        if (precision == 0) {
          make_result(static_cast<int>(result));
        } else {
          make_result(result);
        }
      INJA_OP_TRY_END_GRACEFUL("round")
    } break;
    case Op::Sort: {
      INJA_OP_TRY_BEGIN
        auto result_ptr = std::make_shared<json>(get_arguments<1>(node)[0]->get<std::vector<json>>());
        std::sort(result_ptr->begin(), result_ptr->end());
        data_tmp_stack.push_back(result_ptr);
        data_eval_stack.push(result_ptr.get());
      INJA_OP_TRY_END_GRACEFUL("sort")
    } break;
    case Op::Upper: {
      INJA_OP_TRY_BEGIN
        auto result = get_arguments<1>(node)[0]->get<json::string_t>();
        std::transform(result.begin(), result.end(), result.begin(), [](char c) { return static_cast<char>(::toupper(c)); });
        make_result(std::move(result));
      INJA_OP_TRY_END_GRACEFUL("upper")
    } break;
    case Op::IsBoolean: {
      make_result(get_arguments<1>(node)[0]->is_boolean());
    } break;
    case Op::IsNumber: {
      make_result(get_arguments<1>(node)[0]->is_number());
    } break;
    case Op::IsInteger: {
      make_result(get_arguments<1>(node)[0]->is_number_integer());
    } break;
    case Op::IsFloat: {
      make_result(get_arguments<1>(node)[0]->is_number_float());
    } break;
    case Op::IsObject: {
      make_result(get_arguments<1>(node)[0]->is_object());
    } break;
    case Op::IsArray: {
      make_result(get_arguments<1>(node)[0]->is_array());
    } break;
    case Op::IsString: {
      make_result(get_arguments<1>(node)[0]->is_string());
    } break;
    case Op::Callback: {
      if (!node.callback) {
        // Callback is null - function not found or not registered
        if (config.graceful_errors) {
          // Unknown function without callback in graceful mode
          data_eval_stack.push(nullptr);
          not_found_stack.emplace(node.name, &node);
        } else {
          throw_renderer_error("function '" + node.name + "' not found or has no callback", node);
        }
      } else {
        auto args = get_argument_vector(node);
        // If a callback wrapper is set (for tracing/instrumentation), use it
        if (config.callback_wrapper) {
          make_result(config.callback_wrapper(node.name, args, [&]() {
            return node.callback(args);
          }));
        } else {
          make_result(node.callback(args));
        }
      }
    } break;
    case Op::Super: {
      const auto args = get_argument_vector(node);
      const size_t old_level = current_level;
      const size_t level_diff = (args.size() == 1) ? args[0]->get<int>() : 1;
      const size_t level = current_level + level_diff;

      if (block_statement_stack.empty()) {
        throw_renderer_error("super() call is not within a block", node);
      }

      if (level < 1 || level > template_stack.size() - 1) {
        throw_renderer_error("level of super() call does not match parent templates (between 1 and " + std::to_string(template_stack.size() - 1) + ")", node);
      }

      const auto current_block_statement = block_statement_stack.back();
      const Template* new_template = template_stack.at(level);
      const Template* old_template = current_template;
      const auto block_it = new_template->block_storage.find(current_block_statement->name);
      if (block_it != new_template->block_storage.end()) {
        current_template = new_template;
        current_level = level;
        block_it->second->block.accept(*this);
        current_level = old_level;
        current_template = old_template;
      } else {
        throw_renderer_error("could not find block with name '" + current_block_statement->name + "'", node);
      }
      make_result(nullptr);
    } break;
    case Op::Join: {
      INJA_OP_TRY_BEGIN
        const auto args = get_arguments<2>(node);
        const auto separator = args[1]->get<json::string_t>();
        std::ostringstream os;
        std::string sep;
        for (const auto& value : *args[0]) {
          os << sep;
          if (value.is_string()) {
            os << value.get<std::string>(); // otherwise the value is surrounded with ""
          } else {
            os << value.dump();
          }
          sep = separator;
        }
        make_result(os.str());
      INJA_OP_TRY_END_GRACEFUL("join")
    } break;
    case Op::None: {
      // Unknown function in graceful error mode
      if (config.graceful_errors) {
        // Push nullptr to trigger graceful error handling
        data_eval_stack.push(nullptr);
        not_found_stack.emplace(node.name, &node);
      }
    } break;
    }
  }

  void visit(const ExpressionListNode& node) override {
    auto result = eval_expression_list(node);
    if (result) {
      print_data(result);
    } else if (config.graceful_errors && node.length > 0) {
      // In graceful mode, output the original template text
      *output_stream << current_template->content.substr(node.pos, node.length);
    }
  }

  void visit(const StatementNode&) override {}

  void visit(const ForStatementNode&) override {}

  void visit(const ForArrayStatementNode& node) override {
    const auto result = eval_expression_list(node.condition);
    // In graceful error mode, result can be nullptr if variable is missing
    if (!result) {
      if (config.graceful_errors) {
        return; // Skip the loop if variable is missing in graceful mode
      }
      throw_renderer_error("expression could not be evaluated", node);
    }
    if (!result->is_array()) {
      throw_renderer_error("object must be an array", node);
    }

    emit_event(InstrumentationEvent::ForLoopStart, node.value, "array", result->size());

    if (!current_loop_data->empty()) {
      auto tmp = *current_loop_data; // Because of clang-3
      (*current_loop_data)["parent"] = std::move(tmp);
    }

    size_t index = 0;
    (*current_loop_data)["is_first"] = true;
    (*current_loop_data)["is_last"] = (result->size() <= 1);
    for (auto it = result->begin(); it != result->end(); ++it) {
      additional_data[static_cast<std::string>(node.value)] = *it;

      (*current_loop_data)["index"] = index;
      (*current_loop_data)["index1"] = index + 1;
      if (index == 1) {
        (*current_loop_data)["is_first"] = false;
      }
      if (index == result->size() - 1) {
        (*current_loop_data)["is_last"] = true;
      }

      node.body.accept(*this);
      ++index;
    }

    additional_data[static_cast<std::string>(node.value)].clear();
    if (!(*current_loop_data)["parent"].empty()) {
      const auto tmp = (*current_loop_data)["parent"];
      *current_loop_data = tmp;
    } else {
      current_loop_data = &additional_data["loop"];
    }

    emit_event(InstrumentationEvent::ForLoopEnd, node.value, "array", index);
  }

  void visit(const ForObjectStatementNode& node) override {
    const auto result = eval_expression_list(node.condition);
    // In graceful error mode, result can be nullptr if variable is missing
    if (!result) {
      if (config.graceful_errors) {
        return; // Skip the loop if variable is missing in graceful mode
      }
      throw_renderer_error("expression could not be evaluated", node);
    }
    if (!result->is_object()) {
      throw_renderer_error("object must be an object", node);
    }

    emit_event(InstrumentationEvent::ForLoopStart, node.value, "object", result->size());

    if (!current_loop_data->empty()) {
      (*current_loop_data)["parent"] = std::move(*current_loop_data);
    }

    size_t index = 0;
    (*current_loop_data)["is_first"] = true;
    (*current_loop_data)["is_last"] = (result->size() <= 1);
    for (auto it = result->begin(); it != result->end(); ++it) {
      additional_data[static_cast<std::string>(node.key)] = it.key();
      additional_data[static_cast<std::string>(node.value)] = it.value();

      (*current_loop_data)["index"] = index;
      (*current_loop_data)["index1"] = index + 1;
      if (index == 1) {
        (*current_loop_data)["is_first"] = false;
      }
      if (index == result->size() - 1) {
        (*current_loop_data)["is_last"] = true;
      }

      node.body.accept(*this);
      ++index;
    }

    additional_data[static_cast<std::string>(node.key)].clear();
    additional_data[static_cast<std::string>(node.value)].clear();
    if (!(*current_loop_data)["parent"].empty()) {
      *current_loop_data = std::move((*current_loop_data)["parent"]);
    } else {
      current_loop_data = &additional_data["loop"];
    }

    emit_event(InstrumentationEvent::ForLoopEnd, node.value, "object", index);
  }

  void visit(const IfStatementNode& node) override {
    const auto result = eval_expression_list(node.condition);
    // In graceful error mode, result can be nullptr if variable is missing
    if (result && truthy(result.get())) {
      node.true_statement.accept(*this);
    } else if (node.has_false_statement) {
      node.false_statement.accept(*this);
    }
  }

  void visit(const IncludeStatementNode& node) override {
    emit_event(InstrumentationEvent::IncludeStart, node.file);

    auto sub_renderer = Renderer(config, template_storage, function_storage);
    const auto included_template_it = template_storage.find(node.file);
    if (included_template_it != template_storage.end()) {
      sub_renderer.render_to(*output_stream, included_template_it->second, *data_input, &additional_data);
      emit_event(InstrumentationEvent::IncludeEnd, node.file, "success");
    } else if (config.throw_at_missing_includes) {
      emit_event(InstrumentationEvent::IncludeEnd, node.file, "not_found");
      throw_renderer_error("include '" + node.file + "' not found", node);
    } else {
      emit_event(InstrumentationEvent::IncludeEnd, node.file, "not_found_ignored");
    }
  }

  void visit(const ExtendsStatementNode& node) override {
    const auto included_template_it = template_storage.find(node.file);
    if (included_template_it != template_storage.end()) {
      const Template* parent_template = &included_template_it->second;
      render_to(*output_stream, *parent_template, *data_input, &additional_data);
      break_rendering = true;
    } else if (config.throw_at_missing_includes) {
      throw_renderer_error("extends '" + node.file + "' not found", node);
    }
  }

  void visit(const BlockStatementNode& node) override {
    const size_t old_level = current_level;
    current_level = 0;
    current_template = template_stack.front();
    const auto block_it = current_template->block_storage.find(node.name);
    if (block_it != current_template->block_storage.end()) {
      block_statement_stack.emplace_back(&node);
      block_it->second->block.accept(*this);
      block_statement_stack.pop_back();
    }
    current_level = old_level;
    current_template = template_stack.back();
  }

  /*!
   * \brief Attempts to use in-place optimization for self-assignment patterns.
   *
   * Detects patterns like: {% set items = append(items, x) %}
   * When the first argument of the function is the same variable being assigned,
   * and the function has an in-place callback registered, we can mutate directly
   * instead of copying.
   *
   * @return true if in-place optimization was used, false otherwise
   */
  bool try_inplace_self_assignment(const SetStatementNode& node, const std::string& ptr) {
    // Check if the expression is a function call
    // (Don't emit events for these common non-interesting cases)
    if (!node.expression.root) {
      return false;
    }

    auto* func_node = dynamic_cast<const FunctionNode*>(node.expression.root.get());
    if (!func_node || func_node->operation != FunctionStorage::Operation::Callback) {
      return false;
    }

    // Check if the function has at least one argument
    if (func_node->arguments.empty()) {
      return false;
    }

    // Check if the first argument is a simple variable reference matching our key
    auto* data_node = dynamic_cast<const DataNode*>(func_node->arguments[0].get());
    if (!data_node || data_node->name != node.key) {
      return false;
    }

    // Look up the function and check if it has an in-place callback
    auto func_data = function_storage.find_function(func_node->name, static_cast<int>(func_node->arguments.size()));
    if (func_data.operation != FunctionStorage::Operation::Callback || !func_data.inplace_callback) {
      // This IS interesting - self-assignment pattern detected but function doesn't have in-place variant
      emit_event(InstrumentationEvent::InplaceOptSkipped, node.key, "no_inplace_cb:" + func_node->name);
      return false;
    }

    // Get a mutable reference to the variable
    json::json_pointer json_ptr(ptr);

    // Ensure the variable exists (initialize to null if not)
    if (!additional_data.contains(json_ptr)) {
      // Variable doesn't exist yet - can't do in-place mutation
      // Fall back to normal evaluation which will create it
      emit_event(InstrumentationEvent::InplaceOptSkipped, node.key, "var_not_exists:" + func_node->name);
      return false;
    }

    json& target = additional_data[json_ptr];

    // Evaluate remaining arguments (skip first one since we're using the target directly)
    Arguments remaining_args;
    remaining_args.reserve(func_node->arguments.size() - 1);

    for (size_t i = 1; i < func_node->arguments.size(); ++i) {
      func_node->arguments[i]->accept(*this);
      remaining_args.push_back(data_eval_stack.top());
      data_eval_stack.pop();
    }

    // Call the in-place callback
    if (config.callback_wrapper) {
      // Wrap for tracing/instrumentation
      // Note: we pass a dummy thunk since in-place doesn't return a value
      // IMPORTANT: We return a small placeholder instead of target to avoid copying
      // the potentially large array just for tracing purposes.
      Arguments all_args;
      all_args.push_back(&target);
      for (const auto* arg : remaining_args) {
        all_args.push_back(arg);
      }
      config.callback_wrapper(func_node->name, all_args, [&]() {
        func_data.inplace_callback(target, remaining_args);
        // Return size info instead of full array to avoid O(n) copy
        return json{{"_inplace", true}, {"size", target.is_array() ? target.size() : 0}};
      });
    } else {
      func_data.inplace_callback(target, remaining_args);
    }

    // Emit success event with array size for performance tracking
    size_t target_size = target.is_array() ? target.size() : 0;
    emit_event(InstrumentationEvent::InplaceOptUsed, node.key, func_node->name, target_size);

    return true;
  }

  void visit(const SetStatementNode& node) override {
    emit_event(InstrumentationEvent::SetStatementStart, node.key);

    std::string ptr = node.key;
    replace_substring(ptr, ".", "/");
    ptr = "/" + ptr;

    try {
      // Try in-place optimization first
      if (try_inplace_self_assignment(node, ptr)) {
        emit_event(InstrumentationEvent::SetStatementEnd, node.key, "inplace");
        return;  // Successfully used in-place optimization
      }

      // Fall back to normal evaluation
      auto result = eval_expression_list(node.expression);
      if (result) {
        additional_data[json::json_pointer(ptr)] = *result;
        emit_event(InstrumentationEvent::SetStatementEnd, node.key, "copy");
      } else if (config.graceful_errors) {
        // In graceful error mode, set to null if expression failed
        additional_data[json::json_pointer(ptr)] = nullptr;
        emit_event(InstrumentationEvent::SetStatementEnd, node.key, "null_graceful");
      } else {
        throw_renderer_error("failed to evaluate expression for variable '" + node.key + "'", node);
      }
    } catch (const std::exception& e) {
      if (config.graceful_errors) {
        // In graceful error mode, set to null on exception
        additional_data[json::json_pointer(ptr)] = nullptr;
        emit_event(InstrumentationEvent::SetStatementEnd, node.key, "exception_graceful");
      } else {
        throw_renderer_error("failed to set variable '" + node.key + "': " + e.what(), node);
      }
    } catch (...) {
      if (config.graceful_errors) {
        // Catch all exceptions including SEH
        additional_data[json::json_pointer(ptr)] = nullptr;
        emit_event(InstrumentationEvent::SetStatementEnd, node.key, "unknown_exception");
      } else {
        throw_renderer_error("failed to set variable '" + node.key + "' with unknown exception", node);
      }
    }
  }

  void visit(const RawStatementNode& node) override {
    // Output raw content without any parsing or processing
    output_stream->write(current_template->content.c_str() + node.content_pos, node.content_length);
  }

public:
  explicit Renderer(const RenderConfig& config, const TemplateStorage& template_storage, const FunctionStorage& function_storage)
      : config(config), template_storage(template_storage), function_storage(function_storage) {}

  void render_to(std::ostream& os, const Template& tmpl, const json& data, json* loop_data = nullptr) {
    output_stream = &os;
    current_template = &tmpl;
    data_input = &data;
    if (loop_data != nullptr) {
      additional_data = *loop_data;
      current_loop_data = &additional_data["loop"];
    }

    emit_event(InstrumentationEvent::RenderStart);

    template_stack.emplace_back(current_template);
    current_template->root.accept(*this);

    data_tmp_stack.clear();

    emit_event(InstrumentationEvent::RenderEnd);
  }
  
  const std::vector<RenderErrorInfo>& get_render_errors() const {
    return render_errors;
  }
  
  void clear_render_errors() {
    render_errors.clear();
  }
};

} // namespace inja

#endif // INCLUDE_INJA_RENDERER_HPP_
