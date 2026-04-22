/*
  Handcrafted static evaluation for the flang variant (see Variant::variantTemplate == "flang").

  Clean-room implementation: control-oriented field terms, mobility and material ratio
  statistics, and COMMONER advancement/rush heuristics. Mobility uses attacks_from/moves_from
  and ignores piece freezing (same idea as evaluating pseudo-reach, not legal moves).

  The royal piece in flang is PieceType COMMONER (not KING).

  Score is converted to engine Value (centipawn scale); sign is from White's perspective,
  then adjusted to side-to-move in evaluate_flang().
*/

#ifndef FLANG_EVAL_H_INCLUDED
#define FLANG_EVAL_H_INCLUDED

#include "types.h"

namespace Stockfish {

class Position;

// Static evaluation for flang from the side-to-move perspective (same convention as Eval::evaluate).
Value evaluate_flang(const Position& pos);

} // namespace Stockfish

#endif
