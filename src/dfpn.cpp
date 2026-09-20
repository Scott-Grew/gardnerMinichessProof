#include "dfpn.h"

#include <algorithm>
#include <vector>

#include "canonical.h"
#include "movegen.h"

namespace gardner {

namespace {

// Largest finite value a saturating sum may return, one below
// the infinite sentinel.
constexpr std::uint32_t kSaturatedFiniteMaximum =
    kInfiniteProofNumber - 1;

// Smallest proof (or disproof) number among children, selecting
// which field via use_proof.
std::uint32_t minimum_across_children(
    const std::vector<ProofNumberPair>& children, bool use_proof) {
  std::uint32_t minimum_value = kInfiniteProofNumber;
  for (const ProofNumberPair& child : children) {
    const std::uint32_t value =
        use_proof ? child.proof : child.disproof;
    if (value < minimum_value) minimum_value = value;
  }
  return minimum_value;
}

// Sum of proof (or disproof) numbers across children,
// saturating at kInfiniteProofNumber instead of overflow.
std::uint32_t sum_across_children(
    const std::vector<ProofNumberPair>& children, bool use_proof) {
  std::uint64_t total = 0;
  for (const ProofNumberPair& child : children) {
    const std::uint32_t value =
        use_proof ? child.proof : child.disproof;
    if (value == kInfiniteProofNumber) return kInfiniteProofNumber;
    total += value;
  }
  if (total >= kInfiniteProofNumber) return kSaturatedFiniteMaximum;
  return static_cast<std::uint32_t>(total);
}

// Combines children df-pn style: an OR node takes min-proof,
// sum-disproof; an AND node takes the reverse.
ProofNumberPair combine_children(
    bool is_or_node, const std::vector<ProofNumberPair>& children) {
  if (is_or_node) {
    return ProofNumberPair{minimum_across_children(children, true),
                           sum_across_children(children, false)};
  }
  return ProofNumberPair{sum_across_children(children, true),
                         minimum_across_children(children, false)};
}

// Index of the child with the smallest proof (or disproof)
// number; that child is expanded next.
int select_best_child_index(
    bool is_or_node, const std::vector<ProofNumberPair>& children) {
  int best_index = 0;
  std::uint32_t best_value =
      is_or_node ? children[0].proof : children[0].disproof;
  for (int index = 1; index < static_cast<int>(children.size());
       ++index) {
    const ProofNumberPair& child =
        children[static_cast<std::size_t>(index)];
    const std::uint32_t value =
        is_or_node ? child.proof : child.disproof;
    if (value < best_value) {
      best_value = value;
      best_index = index;
    }
  }
  return best_index;
}

// Smallest proof (or disproof) number among children other
// than best_index; sizes the next search threshold.
std::uint32_t select_second_best_value(
    bool is_or_node, const std::vector<ProofNumberPair>& children,
    int best_index) {
  std::uint32_t second_best_value = kInfiniteProofNumber;
  for (int index = 0; index < static_cast<int>(children.size());
       ++index) {
    if (index == best_index) continue;
    const ProofNumberPair& child =
        children[static_cast<std::size_t>(index)];
    const std::uint32_t value =
        is_or_node ? child.proof : child.disproof;
    if (value < second_best_value) second_best_value = value;
  }
  return second_best_value;
}

// Next child threshold: one more than the second-best sibling,
// capped by the sentinel and the current threshold.
std::uint32_t child_threshold_from_second_best(
    std::uint32_t primary_threshold,
    std::uint32_t second_best_value) {
  // Widened to uint64 so incrementing the sentinel can't
  // overflow before the clamp below.
  const std::uint64_t incremented_second_best =
      static_cast<std::uint64_t>(second_best_value) + 1;
  const std::uint64_t clamped_second_best =
      incremented_second_best > kInfiniteProofNumber
          ? kInfiniteProofNumber
          : incremented_second_best;
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      primary_threshold, clamped_second_best));
}

// Next child threshold, reduced by how much room the parent's
// sum has left for this child, clamped to [0, sentinel].
std::uint32_t child_threshold_from_sum_reduction(
    std::uint32_t primary_threshold, std::uint32_t own_sum,
    std::uint32_t best_child_value) {
  if (primary_threshold == kInfiniteProofNumber) {
    return kInfiniteProofNumber;
  }
  // Signed arithmetic since the subtraction can go negative
  // before the best child's value is added back.
  const std::int64_t candidate =
      static_cast<std::int64_t>(primary_threshold) -
      static_cast<std::int64_t>(own_sum) +
      static_cast<std::int64_t>(best_child_value);
  if (candidate < 0) return 0;
  if (candidate > kInfiniteProofNumber) return kInfiniteProofNumber;
  return static_cast<std::uint32_t>(candidate);
}

// RAII guard removing a position's key from the search path
// on return, so cycle detection stays scoped to this subtree.
class PathGuard {
 public:
  // Binds to path_keys and remembers key to erase later.
  PathGuard(
      std::unordered_set<PositionKey, PositionKeyHash>& path_keys,
      PositionKey key)
      : path_keys_(path_keys), key_(key) {}
  // Erases key from path_keys when this frame returns.
  ~PathGuard() { path_keys_.erase(key_); }

  PathGuard(const PathGuard&) = delete;
  PathGuard& operator=(const PathGuard&) = delete;

 private:
  std::unordered_set<PositionKey, PositionKeyHash>& path_keys_;
  PositionKey key_;
};

}

// Constructs a search over table with node_limit visits
// allowed (0 means unlimited).
ProofSearch::ProofSearch(SharedTable& table, std::uint64_t node_limit)
    : table_(table),
      node_limit_(node_limit),
      node_limit_exceeded_(false),
      statistics_{0, 0} {}

// Core df-pn recursion, bounded by proof_threshold and
// disproof_threshold; stores the resolved pair for this node.
ProofNumberPair ProofSearch::visit(const Position& position,
                                   std::uint32_t proof_threshold,
                                   std::uint32_t disproof_threshold) {
  const std::uint64_t nodes_visited_before_this_call =
      statistics_.nodes_visited;
  ++statistics_.nodes_visited;
  if (node_limit_ != 0 && statistics_.nodes_visited > node_limit_) {
    node_limit_exceeded_ = true;
    return ProofNumberPair{1, 1};
  }

  const CanonicalForm canonical_form =
      canonical_under_mirror(position);
  const PositionKey canonical_key =
      exact_key(canonical_form.position);

  const MoveList legal_moves = generate_legal_moves(position);
  // No legal moves: proved only if Black is checkmated; every
  // other no-move case (stalemate, White checkmated) disproves.
  if (legal_moves.size() == 0) {
    const bool black_to_move_in_check =
        position.side_to_move == Color::Black &&
        is_in_check(position, Color::Black);
    const ProofNumberPair terminal_pair =
        black_to_move_in_check
            ? ProofNumberPair{0, kInfiniteProofNumber}
            : ProofNumberPair{kInfiniteProofNumber, 0};
    store_entry(canonical_key, terminal_pair, Move{},
                nodes_visited_before_this_call);
    return terminal_pair;
  }

  // A repeat on this path counts against the prover (disproof
  // 0) and is deliberately not stored in the table.
  if (path_keys_.contains(canonical_key)) {
    ++statistics_.repetition_hits;
    return ProofNumberPair{kInfiniteProofNumber, 0};
  }
  path_keys_.insert(canonical_key);
  const PathGuard path_guard(path_keys_, canonical_key);

  const std::uint64_t repetition_hits_before_this_call =
      statistics_.repetition_hits;

  const bool is_or_node = position.side_to_move == Color::White;
  const int child_count = legal_moves.size();
  std::vector<ProofNumberPair> child_pairs(
      static_cast<std::size_t>(child_count));
  for (int child_index = 0; child_index < child_count;
       ++child_index) {
    const Position child_position =
        make_move(position, legal_moves[child_index]);
    const PositionKey child_key =
        exact_key(canonical_under_mirror(child_position).position);
    const std::optional<TableEntry> found_entry =
        table_.find(child_key);
    child_pairs[static_cast<std::size_t>(child_index)] =
        found_entry.has_value()
            ? ProofNumberPair{found_entry->proof,
                              found_entry->disproof}
            : ProofNumberPair{1, 1};
  }

  ProofNumberPair node_pair =
      combine_children(is_or_node, child_pairs);
  while (node_pair.proof != 0 && node_pair.disproof != 0 &&
         node_pair.proof < proof_threshold &&
         node_pair.disproof < disproof_threshold) {
    const int best_child_index =
        select_best_child_index(is_or_node, child_pairs);
    const std::uint32_t second_best_value = select_second_best_value(
        is_or_node, child_pairs, best_child_index);

    std::uint32_t child_proof_threshold;
    std::uint32_t child_disproof_threshold;
    if (is_or_node) {
      child_proof_threshold = child_threshold_from_second_best(
          proof_threshold, second_best_value);
      child_disproof_threshold = child_threshold_from_sum_reduction(
          disproof_threshold, node_pair.disproof,
          child_pairs[static_cast<std::size_t>(best_child_index)]
              .disproof);
    } else {
      child_disproof_threshold = child_threshold_from_second_best(
          disproof_threshold, second_best_value);
      child_proof_threshold = child_threshold_from_sum_reduction(
          proof_threshold, node_pair.proof,
          child_pairs[static_cast<std::size_t>(best_child_index)]
              .proof);
    }

    const Position best_child_position =
        make_move(position, legal_moves[best_child_index]);
    const ProofNumberPair returned_pair =
        visit(best_child_position, child_proof_threshold,
              child_disproof_threshold);
    if (node_limit_exceeded_) {
      return ProofNumberPair{1, 1};
    }
    child_pairs[static_cast<std::size_t>(best_child_index)] =
        returned_pair;
    node_pair = combine_children(is_or_node, child_pairs);
  }

  const bool repetition_occurred_beneath_this_call =
      statistics_.repetition_hits > repetition_hits_before_this_call;
  // Skip storing a disproof that depended on a repetition below
  // it, so every stored disproof stays path-independent.
  if (!(node_pair.disproof == 0 &&
        repetition_occurred_beneath_this_call)) {
    const int stored_best_index =
        select_best_child_index(is_or_node, child_pairs);
    const Move best_move_in_search_frame =
        legal_moves[stored_best_index];
    const Move best_move_in_canonical_frame =
        canonical_form.was_mirrored
            ? mirror_files_move(best_move_in_search_frame)
            : best_move_in_search_frame;
    store_entry(canonical_key, node_pair,
                best_move_in_canonical_frame,
                nodes_visited_before_this_call);
  }

  return node_pair;
}

// Records a node's resolved pair and best move in the shared
// table, tagging it with the work (nodes) spent below it.
void ProofSearch::store_entry(
    const PositionKey& key, ProofNumberPair pair, Move best_move,
    std::uint64_t nodes_visited_before_this_call) {
  const std::uint64_t work =
      statistics_.nodes_visited - nodes_visited_before_this_call;
  const std::uint32_t saturated_work =
      work > kInfiniteProofNumber ? kInfiniteProofNumber
                                  : static_cast<std::uint32_t>(work);
  table_.store(TableEntry{key, pair.proof, pair.disproof,
                          saturated_work, best_move, true});
}

// Runs visit() from the root with unbounded thresholds and
// classifies the result as Proved, Disproved, or Unknown.
ProofResult ProofSearch::prove(const Position& normalised_root) {
  node_limit_exceeded_ = false;
  const ProofNumberPair root_pair = visit(
      normalised_root, kInfiniteProofNumber, kInfiniteProofNumber);
  if (root_pair.proof == 0) return ProofResult::Proved;
  if (root_pair.disproof == 0) return ProofResult::Disproved;
  return ProofResult::Unknown;
}

// True if position's canonical key is a proved table entry,
// or it is an immediate Black checkmate not yet stored.
bool ProofSearch::is_proven(const Position& position) const {
  const PositionKey canonical_key =
      exact_key(canonical_under_mirror(position).position);
  const std::optional<TableEntry> found_entry =
      table_.find(canonical_key);
  if (found_entry.has_value() && found_entry->proof == 0) return true;
  return position.side_to_move == Color::Black &&
         generate_legal_moves(position).size() == 0 &&
         is_in_check(position, Color::Black);
}

// The first legal move whose resulting position is_proven;
// nullopt if none has been proved yet.
std::optional<Move> ProofSearch::proving_move(
    const Position& position) const {
  for (const Move move : generate_legal_moves(position)) {
    if (is_proven(make_move(position, move))) return move;
  }
  return std::nullopt;
}

// This search's running node and repetition counters.
const SearchStatistics& ProofSearch::statistics() const {
  return statistics_;
}

}
