#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "board.h"

namespace gardner {

// One df-pn table slot: key, proof/disproof pair, work spent,
// best move, and whether the slot is occupied.
struct TableEntry {
  PositionKey key;
  std::uint32_t proof;
  std::uint32_t disproof;
  std::uint32_t work;
  Move best_move;
  bool occupied;
};

std::uint64_t mix_position_key(const PositionKey& key);

// Fixed-size, power-of-two-bucketed hash table mapping a
// position's exact key to its most recent df-pn entry.
class SharedTable {
 public:
  explicit SharedTable(std::size_t megabytes);

  std::optional<TableEntry> find(const PositionKey& key) const;
  void store(const TableEntry& entry);
  std::size_t occupied_count() const;

 private:
  std::vector<TableEntry> entries_;
  std::size_t index_mask_;
  std::size_t occupied_count_;
};

}
