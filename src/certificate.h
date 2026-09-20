#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "board.h"
#include "dfpn.h"

namespace gardner {

// One certificate row: a key and its stored move, or 255/255
// when the position has no stored move (Black to move).
struct CertificateEntry {
  PositionKey key;
  std::uint8_t from_square;
  std::uint8_t to_square;
  std::uint8_t promotion_code;
};

std::optional<std::vector<CertificateEntry>> extract_certificate(
    ProofSearch& search, const Position& normalised_root);

bool write_certificate(const std::string& path,
                       const std::vector<CertificateEntry>& entries);

std::array<std::uint64_t, 26> men_histogram(
    const std::vector<CertificateEntry>& entries);

}
