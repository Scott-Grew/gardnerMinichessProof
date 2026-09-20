#include "shared_table.h"

#include <limits>

namespace gardner {

namespace {

// Slots probed per bucket before falling back to eviction.
constexpr std::size_t kBucketSize = 4;

// Largest power-of-two entry count that fits within the
// requested megabytes.
std::size_t entry_capacity_for_megabytes(std::size_t megabytes) {
  const std::size_t total_bytes =
      megabytes * std::size_t{1024} * std::size_t{1024};
  const std::size_t maximum_entries =
      total_bytes / sizeof(TableEntry);
  std::size_t capacity = 1;
  while (capacity * 2 <= maximum_entries) {
    capacity *= 2;
  }
  return capacity;
}

}

// 64-bit avalanche mix of a position's key, used both as the
// hash table's bucket index and PositionKeyHash.
std::uint64_t mix_position_key(const PositionKey& key) {
  std::uint64_t mixed_value =
      key.low ^ (key.high * 0x9E3779B97F4A7C15ULL);
  mixed_value ^= mixed_value >> 30;
  mixed_value *= 0xBF58476D1CE4E5B9ULL;
  mixed_value ^= mixed_value >> 27;
  mixed_value *= 0x94D049BB133111EBULL;
  mixed_value ^= mixed_value >> 31;
  return mixed_value;
}

// Allocates entries_ sized to the requested megabytes,
// rounded down to a power of two for the index mask.
SharedTable::SharedTable(std::size_t megabytes)
    : entries_(entry_capacity_for_megabytes(megabytes)),
      index_mask_(entries_.size() - 1),
      occupied_count_(0) {}

// Looks up key's bucket and linearly scans its 4 slots for
// an occupied, matching entry.
std::optional<TableEntry> SharedTable::find(
    const PositionKey& key) const {
  const std::size_t bucket_start =
      static_cast<std::size_t>(mix_position_key(key)) & index_mask_;
  // Wraps around the table via the power-of-two index mask.
  for (std::size_t slot_offset = 0; slot_offset < kBucketSize;
       ++slot_offset) {
    const std::size_t slot_index =
        (bucket_start + slot_offset) & index_mask_;
    const TableEntry& slot = entries_[slot_index];
    if (slot.occupied && slot.key == key) {
      return slot;
    }
  }
  return std::nullopt;
}

// Inserts or updates entry: prefers a matching key's slot,
// then an empty slot, else evicts the bucket's least-work slot.
void SharedTable::store(const TableEntry& entry) {
  const std::size_t bucket_start =
      static_cast<std::size_t>(mix_position_key(entry.key)) &
      index_mask_;
  std::optional<std::size_t> matching_key_slot;
  std::optional<std::size_t> unoccupied_slot;
  std::size_t smallest_work_slot = bucket_start;
  std::uint32_t smallest_work =
      std::numeric_limits<std::uint32_t>::max();
  // Wraps around the table via the power-of-two index mask.
  for (std::size_t slot_offset = 0; slot_offset < kBucketSize;
       ++slot_offset) {
    const std::size_t slot_index =
        (bucket_start + slot_offset) & index_mask_;
    const TableEntry& slot = entries_[slot_index];
    if (slot.occupied && slot.key == entry.key) {
      matching_key_slot = slot_index;
      break;
    }
    if (!slot.occupied && !unoccupied_slot.has_value()) {
      unoccupied_slot = slot_index;
    }
    if (slot.work < smallest_work) {
      smallest_work = slot.work;
      smallest_work_slot = slot_index;
    }
  }

  std::size_t target_slot_index;
  if (matching_key_slot.has_value()) {
    target_slot_index = *matching_key_slot;
  } else if (unoccupied_slot.has_value()) {
    target_slot_index = *unoccupied_slot;
  } else {
    target_slot_index = smallest_work_slot;
  }

  if (!entries_[target_slot_index].occupied) {
    ++occupied_count_;
  }
  entries_[target_slot_index] = entry;
  entries_[target_slot_index].occupied = true;
}

// Number of slots currently holding an entry.
std::size_t SharedTable::occupied_count() const {
  return occupied_count_;
}

}
