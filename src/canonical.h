#pragma once

#include "board.h"

namespace gardner {

// Result of mirroring a position to its canonical form: the
// chosen position and whether it was the mirrored one.
struct CanonicalForm {
  Position position;
  bool was_mirrored;
};

Position mirror_files(const Position& position);
Position swap_colors(const Position& position);
Move mirror_files_move(Move move);
Move swap_colors_move(Move move);
Position defender_as_white(const Position& position, Color defender);
CanonicalForm canonical_under_mirror(const Position& position);

}
