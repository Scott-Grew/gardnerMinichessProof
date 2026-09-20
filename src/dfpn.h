#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_set>

#include "board.h"
#include "shared_table.h"

namespace gardner {

// Sentinel meaning an infinite (unreached) proof or disproof
// number.
constexpr std::uint32_t kInfiniteProofNumber =
    std::numeric_limits<std::uint32_t>::max();

// Outcome of a proof search: the root position is proved,
// disproved, or unresolved within the given node limit.
enum class ProofResult { Proved, Disproved, Unknown };

// Running counters for one proof search: total nodes visited
// and how many times the search hit a repeated position.
struct SearchStatistics {
  std::uint64_t nodes_visited;
  std::uint64_t repetition_hits;
};

// A node's proof number (0 once proved) and disproof number
// (0 once disproved); both saturate at kInfiniteProofNumber.
struct ProofNumberPair {
  std::uint32_t proof;
  std::uint32_t disproof;
};

// Hashes a PositionKey for use as an unordered_set key, via
// the table's key-mixing function.
struct PositionKeyHash {
  // Mixes a key through the table's hash function.
  std::size_t operator()(const PositionKey& key) const {
    return static_cast<std::size_t>(mix_position_key(key));
  }
};

// Serial df-pn proof-number search; the position passed to
// prove() must already have the defender normalised to White.
class ProofSearch {
 public:
  ProofSearch(SharedTable& table, std::uint64_t node_limit);

  ProofResult prove(const Position& normalised_root);
  bool is_proven(const Position& position) const;
  std::optional<Move> proving_move(const Position& position) const;
  const SearchStatistics& statistics() const;

 private:
  ProofNumberPair visit(const Position& position,
                        std::uint32_t proof_threshold,
                        std::uint32_t disproof_threshold);
  void store_entry(const PositionKey& key, ProofNumberPair pair,
                   Move best_move,
                   std::uint64_t nodes_visited_before_this_call);

  SharedTable& table_;
  std::uint64_t node_limit_;
  bool node_limit_exceeded_;
  SearchStatistics statistics_;
  std::unordered_set<PositionKey, PositionKeyHash> path_keys_;
};

}
