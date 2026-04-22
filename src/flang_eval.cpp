/*
  Flang handcrafted evaluation (clean-room).

  Mobility / threat generation uses attacks_from() and moves_from() only; those
  do not consult piece freezing, so pseudo-reach matches an "ignore freeze"
  static mobility (legal play still applies movegen + frozen_piece_square()).
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "bitboard.h"
#include "flang_eval.h"
#include "position.h"

namespace Stockfish {

namespace {

// Term weights (tuned for centipawn-scale output via final rounding).
constexpr double FLANG_FACTOR_MATRIX      = 2.0;
constexpr double FLANG_FACTOR_MOVEMENT    = 10.0;
constexpr double FLANG_FACTOR_PIECE_VALUE = 140.0;
constexpr double FLANG_FACTOR_KINGS_EVAL    = 2.0;
constexpr double FLANG_BASE_THREAT          = 10000.0;
constexpr std::int64_t FLANG_THREAT_SCALE  = 1000000;

constexpr double FLANG_MATE_SCORE           = 10000.0; // nominal static "mate" margin for decided positions

struct Field {
    std::int64_t whiteThreat = 0;
    std::int64_t blackThreat = 0;
    int          occupiedBy  = 0; // signed midgame piece value, white positive
    double       weight      = 0.0;
};

// Pseudo-reach count for one side: sum of destination squares over all pieces (freeze ignored).
int count_reach(const Position& pos, Color c) {
    int total = 0;
    Bitboard bb = pos.pieces(c);
    while (bb)
    {
        Square   s  = pop_lsb(bb);
        Piece    pc = pos.piece_on(s);
        PieceType pt = type_of(pc);
        Bitboard dest =
            pos.attacks_from(c, pt, s) | pos.moves_from(c, pt, s);
        total += popcount(dest);
    }
    return std::max(total, 1);
}

double mg_value_piece(Piece pc) {
    return std::abs((double) EvalPieceValue[MG][pc]);
}

double evaluate_field(const Field& f) {
    const double wc = double(f.whiteThreat) + FLANG_BASE_THREAT;
    const double bc = double(f.blackThreat) + FLANG_BASE_THREAT;
    const double controlRate = (wc / bc) - (bc / wc);
    const double w           = f.weight * 0.6 + 1.0;

    double result = 0.0;
    if (f.occupiedBy > 0)
    {
        const int factor =
            (f.blackThreat > f.whiteThreat) ? f.occupiedBy : 100;
        result = (1.0 + controlRate) * double(factor) / 100.0;
    }
    else if (f.occupiedBy < 0)
    {
        const int factor =
            (f.whiteThreat > f.blackThreat) ? -f.occupiedBy : 100;
        result = (-1.0 + controlRate) * double(factor) / 100.0;
    }
    else
        result = controlRate;

    return result * w;
}

} // namespace

Value evaluate_flang(const Position& pos) {

    assert(pos.variant());
    assert(pos.variant()->variantTemplate == "flang");

    Value terminal;
    if (pos.is_immediate_game_end(terminal))
        return terminal;

    // Decisive static cases: missing COMMONER (extinction).
    if (!pos.pieces(WHITE, COMMONER))
        return Value(-int(FLANG_MATE_SCORE));
    if (!pos.pieces(BLACK, COMMONER))
        return Value(int(FLANG_MATE_SCORE));

    std::array<Field, SQUARE_NB> fields{};

    double whiteMaterial = 1.0;
    double blackMaterial = 1.0;

    // Per-square occupancy sign and piece-type stats; distribute threat to target squares.
    Bitboard occ = pos.pieces();
    while (occ)
    {
        Square    s  = pop_lsb(occ);
        Piece     pc = pos.piece_on(s);
        Color     c  = color_of(pc);
        PieceType pt = type_of(pc);
        double    v  = mg_value_piece(pc);

        if (c == WHITE)
            whiteMaterial += v;
        else
            blackMaterial += v;

        fields[s].occupiedBy = (c == WHITE) ? int(v) : -int(v);

        const std::int64_t thr =
            FLANG_THREAT_SCALE / std::max(1, int(v));
        Bitboard dest = pos.attacks_from(c, pt, s) | pos.moves_from(c, pt, s);
        while (dest)
        {
            Square t = pop_lsb(dest);
            if (c == WHITE)
                fields[t].whiteThreat += thr;
            else
                fields[t].blackThreat += thr;
        }
    }

    // COMMONER "rush": weight files toward promotion / back rank (exponential by rank).
    Square wksq = pos.square<COMMONER>(WHITE);
    Square bksq = pos.square<COMMONER>(BLACK);

    const int wExp = 1 << rank_of(wksq);
    const int bExp = 1 << (pos.max_rank() - rank_of(bksq));

    // Weight the file toward promotion: white from commoner rank up to one rank below board top.
    const Rank whiteRushEnd = Rank(std::max(0, int(pos.max_rank()) - 1));
    for (Rank r = rank_of(wksq); r <= whiteRushEnd; ++r)
    {
        Square sq = make_square(file_of(wksq), r);
        fields[sq].weight += double(wExp);
    }
    // Black: from commoner rank down to second rank (exclude white's back rank), matching client ranges.
    for (Rank r = rank_of(bksq); r >= RANK_2; --r)
    {
        Square sq = make_square(file_of(bksq), r);
        fields[sq].weight += double(bExp);
    }

    double matrix = 0.0;
    Bitboard bregion = pos.board_bb();
    while (bregion)
    {
        Square s = pop_lsb(bregion);
        matrix += evaluate_field(fields[s]);
    }

    const double wm = double(count_reach(pos, WHITE));
    const double bm = double(count_reach(pos, BLACK));
    const double movementEval =
        FLANG_FACTOR_MOVEMENT * ((wm / bm) - (bm / wm));

    const double pieceValEval = FLANG_FACTOR_PIECE_VALUE
                              * ((whiteMaterial / blackMaterial)
                                 - (blackMaterial / whiteMaterial));

    const double kingScalar = FLANG_FACTOR_KINGS_EVAL
                            * ((double(wExp) / double(bExp))
                               - (double(bExp) / double(wExp)));

    double eval = FLANG_FACTOR_MATRIX * matrix + movementEval + pieceValEval + kingScalar;

    // Thousandths rounding (keeps eval stable for TT).
    eval = std::trunc(eval * 1000.0) / 1000.0;

    Value v = Value(std::lround(eval));

    // Side to move (same convention as classical Evaluation::value()).
    v = (pos.side_to_move() == WHITE ? v : -v);

    return v;
}

} // namespace Stockfish
