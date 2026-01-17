#ifndef INCLUDE_INJA_CALLBACK_CACHE_HPP_
#define INCLUDE_INJA_CALLBACK_CACHE_HPP_

#include <chrono>
#include <functional>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "json.hpp"
#include "config.hpp"
#include "function_storage.hpp"

namespace inja {

/*!
 * \brief Configuration for callback caching behavior.
 */
struct CallbackCacheConfig {
  /// Time-to-live for cached entries (default: 5 seconds)
  std::chrono::milliseconds ttl{5000};

  /// Maximum number of entries in the cache (0 = unlimited)
  size_t max_entries{10000};

  /// Whether to cache void callbacks (callbacks that return empty json)
  /// Generally should be false since void callbacks are for side effects
  bool cache_void_callbacks{false};
};

/*!
 * \brief Thread-safe LRU cache with TTL for callback results.
 *
 * This cache stores the results of callback function calls, keyed by
 * the function name and serialized arguments. It uses:
 * - LRU eviction when max_entries is reached
 * - TTL-based expiration for freshness
 * - Read-write locking for thread safety (readers don't block each other)
 *
 * Usage:
 * @code
 * CallbackCache cache(CallbackCacheConfig{
 *     .ttl = std::chrono::seconds(5),
 *     .max_entries = 10000
 * });
 *
 * // Use as a callback wrapper
 * env.set_callback_wrapper(cache.make_caching_wrapper());
 * @endcode
 */
class CallbackCache {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  /// Predicate function to determine if a callback should be cached
  /// Return true to cache, false to skip caching
  using CachePredicate = std::function<bool(const std::string& function_name)>;

private:
  struct CacheEntry {
    json value;
    TimePoint expiry;
    std::string key;
  };

  // LRU list: front = most recently used, back = least recently used
  using LruList = std::list<CacheEntry>;
  using LruIterator = LruList::iterator;

  // Map from cache key to LRU list iterator
  std::unordered_map<std::string, LruIterator> cache_map_;
  LruList lru_list_;

  mutable std::shared_mutex mutex_;
  CallbackCacheConfig config_;
  CachePredicate should_cache_;

  // Statistics
  mutable std::atomic<uint64_t> hits_{0};
  mutable std::atomic<uint64_t> misses_{0};
  mutable std::atomic<uint64_t> evictions_{0};

  /*!
   * \brief Generates a cache key from function name and arguments.
   *
   * The key format is: "function_name:arg1_json,arg2_json,..."
   * Arguments are serialized to compact JSON strings.
   */
  static std::string make_cache_key(const std::string& function_name, const Arguments& args) {
    std::string key = function_name;
    key.reserve(function_name.size() + args.size() * 32); // Estimate

    key += ':';
    bool first = true;
    for (const auto* arg : args) {
      if (!first) {
        key += ',';
      }
      first = false;

      if (arg) {
        // Use compact JSON serialization
        key += arg->dump(-1);
      } else {
        key += "null";
      }
    }

    return key;
  }

  /*!
   * \brief Removes expired entries (called while holding write lock).
   */
  void remove_expired_entries_locked() {
    const auto now = Clock::now();

    // Remove from back (least recently used) while expired
    while (!lru_list_.empty() && lru_list_.back().expiry <= now) {
      cache_map_.erase(lru_list_.back().key);
      lru_list_.pop_back();
      ++evictions_;
    }
  }

  /*!
   * \brief Evicts entries if over capacity (called while holding write lock).
   */
  void evict_if_needed_locked() {
    if (config_.max_entries == 0) {
      return; // Unlimited
    }

    while (cache_map_.size() >= config_.max_entries && !lru_list_.empty()) {
      cache_map_.erase(lru_list_.back().key);
      lru_list_.pop_back();
      ++evictions_;
    }
  }

public:
  /*!
   * \brief Constructs a callback cache with the given configuration.
   */
  explicit CallbackCache(const CallbackCacheConfig& config = CallbackCacheConfig{})
      : config_(config) {}

  /*!
   * \brief Sets a predicate function to determine which callbacks should be cached.
   *
   * @param predicate Function that returns true if the callback should be cached.
   *                  If not set, all callbacks are cached.
   *
   * Example:
   * @code
   * cache.set_cache_predicate([](const std::string& name) {
   *     // Don't cache callbacks with side effects
   *     return name != "random" && name != "capture_screenshot";
   * });
   * @endcode
   */
  void set_cache_predicate(CachePredicate predicate) {
    should_cache_ = std::move(predicate);
  }

  /*!
   * \brief Attempts to get a cached value.
   *
   * @param function_name The name of the callback function
   * @param args The arguments passed to the callback
   * @param[out] result The cached result if found and not expired
   * @return true if a valid cached value was found, false otherwise
   */
  bool try_get(const std::string& function_name, const Arguments& args, json& result) {
    const std::string key = make_cache_key(function_name, args);
    const auto now = Clock::now();

    // Try read lock first for cache hit (common case)
    {
      std::shared_lock<std::shared_mutex> lock(mutex_);

      auto it = cache_map_.find(key);
      if (it != cache_map_.end() && it->second->expiry > now) {
        result = it->second->value;
        ++hits_;
        return true;
      }
    }

    ++misses_;
    return false;
  }

  /*!
   * \brief Stores a value in the cache.
   *
   * @param function_name The name of the callback function
   * @param args The arguments passed to the callback
   * @param value The result to cache
   */
  void put(const std::string& function_name, const Arguments& args, const json& value) {
    // Don't cache void/empty results unless configured to
    if (!config_.cache_void_callbacks && value.is_null()) {
      return;
    }

    const std::string key = make_cache_key(function_name, args);
    const auto expiry = Clock::now() + config_.ttl;

    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Remove expired entries periodically
    remove_expired_entries_locked();

    // Check if key already exists
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
      // Update existing entry and move to front
      it->second->value = value;
      it->second->expiry = expiry;
      lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    } else {
      // Evict if needed before inserting
      evict_if_needed_locked();

      // Insert new entry at front
      lru_list_.push_front(CacheEntry{value, expiry, key});
      cache_map_[key] = lru_list_.begin();
    }
  }

  /*!
   * \brief Clears all cached entries.
   */
  void clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_map_.clear();
    lru_list_.clear();
  }

  /*!
   * \brief Invalidates a specific callback's cached entries.
   *
   * This removes all cached entries for the given function name,
   * regardless of arguments.
   *
   * @param function_name The callback function name to invalidate
   * @return Number of entries removed
   */
  size_t invalidate(const std::string& function_name) {
    const std::string prefix = function_name + ":";
    size_t removed = 0;

    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (auto it = lru_list_.begin(); it != lru_list_.end(); ) {
      if (it->key.compare(0, prefix.size(), prefix) == 0) {
        cache_map_.erase(it->key);
        it = lru_list_.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }

    return removed;
  }

  /*!
   * \brief Creates a CallbackWrapper that caches callback results.
   *
   * This wrapper can be passed to Environment::set_callback_wrapper().
   * It will cache results based on function name and arguments.
   *
   * @return A CallbackWrapper function that provides caching
   *
   * Example:
   * @code
   * auto cache = std::make_shared<CallbackCache>();
   * env.set_callback_wrapper(cache->make_caching_wrapper());
   * @endcode
   */
  CallbackWrapper make_caching_wrapper() {
    return [this](const std::string& function_name,
                  const Arguments& args,
                  const std::function<json()>& callback_thunk) -> json {
      // Check predicate first
      if (should_cache_ && !should_cache_(function_name)) {
        return callback_thunk();
      }

      // Try to get from cache
      json cached_result;
      if (try_get(function_name, args, cached_result)) {
        return cached_result;
      }

      // Cache miss - execute callback
      json result = callback_thunk();

      // Store in cache
      put(function_name, args, result);

      return result;
    };
  }

  /*!
   * \brief Creates a CallbackWrapper that combines caching with another wrapper.
   *
   * This allows chaining caching with other instrumentation (e.g., tracing).
   * The inner wrapper is called on cache misses.
   *
   * @param inner_wrapper The wrapper to call on cache misses (e.g., for tracing)
   * @return A CallbackWrapper that caches and delegates to inner_wrapper
   *
   * Example:
   * @code
   * auto cache = std::make_shared<CallbackCache>();
   * auto tracing_wrapper = [&ctx](const std::string& name, const Arguments& args,
   *                               const std::function<json()>& thunk) {
   *     auto span = ctx.StartSpan("decorator:" + name);
   *     auto result = thunk();
   *     ctx.EndSpan(span);
   *     return result;
   * };
   * env.set_callback_wrapper(cache->make_caching_wrapper_with_inner(tracing_wrapper));
   * @endcode
   */
  CallbackWrapper make_caching_wrapper_with_inner(const CallbackWrapper& inner_wrapper) {
    return [this, inner_wrapper](const std::string& function_name,
                                  const Arguments& args,
                                  const std::function<json()>& callback_thunk) -> json {
      // Check predicate first
      if (should_cache_ && !should_cache_(function_name)) {
        if (inner_wrapper) {
          return inner_wrapper(function_name, args, callback_thunk);
        }
        return callback_thunk();
      }

      // Try to get from cache
      json cached_result;
      if (try_get(function_name, args, cached_result)) {
        return cached_result;
      }

      // Cache miss - execute through inner wrapper if present
      json result;
      if (inner_wrapper) {
        result = inner_wrapper(function_name, args, callback_thunk);
      } else {
        result = callback_thunk();
      }

      // Store in cache
      put(function_name, args, result);

      return result;
    };
  }

  // Statistics accessors

  /// Returns the number of cache hits
  uint64_t hits() const { return hits_.load(); }

  /// Returns the number of cache misses
  uint64_t misses() const { return misses_.load(); }

  /// Returns the number of evictions (TTL expiry or LRU eviction)
  uint64_t evictions() const { return evictions_.load(); }

  /// Returns the current number of entries in the cache
  size_t size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cache_map_.size();
  }

  /// Returns the hit rate as a percentage (0.0 to 1.0)
  double hit_rate() const {
    const uint64_t h = hits_.load();
    const uint64_t m = misses_.load();
    const uint64_t total = h + m;
    return total > 0 ? static_cast<double>(h) / static_cast<double>(total) : 0.0;
  }

  /// Resets all statistics counters
  void reset_stats() {
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
  }

  /// Returns the cache configuration
  const CallbackCacheConfig& config() const { return config_; }
};

/*!
 * \brief Factory function to create a caching callback wrapper.
 *
 * This is a convenience function for simple use cases where you don't
 * need to manage the cache separately.
 *
 * @param config Cache configuration
 * @param predicate Optional predicate to filter which callbacks are cached
 * @return A pair of {CallbackWrapper, shared_ptr<CallbackCache>}
 *
 * Example:
 * @code
 * auto [wrapper, cache] = inja::make_caching_callback_wrapper(
 *     CallbackCacheConfig{.ttl = std::chrono::seconds(5)},
 *     [](const std::string& name) { return name != "random"; }
 * );
 * env.set_callback_wrapper(wrapper);
 *
 * // Later, check statistics
 * std::cout << "Hit rate: " << cache->hit_rate() * 100 << "%" << std::endl;
 * @endcode
 */
inline std::pair<CallbackWrapper, std::shared_ptr<CallbackCache>>
make_caching_callback_wrapper(
    const CallbackCacheConfig& config = CallbackCacheConfig{},
    CallbackCache::CachePredicate predicate = nullptr) {

  auto cache = std::make_shared<CallbackCache>(config);
  if (predicate) {
    cache->set_cache_predicate(std::move(predicate));
  }
  return std::make_pair(cache->make_caching_wrapper(), cache);
}

} // namespace inja

#endif // INCLUDE_INJA_CALLBACK_CACHE_HPP_
