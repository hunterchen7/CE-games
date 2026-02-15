/*
 * Chess Engine CE Benchmark
 *
 * Unified profiling harness for the chess engine on TI-84 Plus CE.
 * Uses hardware timers (48 MHz) for cycle-accurate measurements.
 * Output to both screen (graphx) and emulator debug console (dbg_printf).
 *
 * Sections:
 *   1. Memory    — structure sizes
 *   2. Ops       — single-call timing for individual operations
 *   3. Components — iterated benchmarks (movegen, eval, make/unmake)
 *   4. Perft     — node counting at multiple depths
 *   5. Search    — depth-limited search benchmarks
 *
 * Build: cd chess/bench && make
 * Run:   cargo run --release --example debug -- run chess/bench/bin/BENCH.8xp ../libs/[8xv files]
 */

#undef NDEBUG
#include <debug.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/timers.h>
#include <graphx.h>

/* Internal engine headers for component benchmarking */
#include "board.h"
#include "movegen.h"
#include "eval.h"
#include "search.h"
#include "zobrist.h"
#include "tt.h"
#include "engine.h"

/* ========== Time Function (48 MHz hardware timer) ========== */

static uint32_t bench_time_ms(void)
{
    return timer_GetSafe(1, TIMER_UP) / 48000UL;
}

/* ========== FEN Parser (into board_t directly) ========== */

static void parse_fen_board(const char *fen, board_t *b)
{
    int8_t ui_board[8][8];
    int8_t turn = 1;
    uint8_t castling = 0;
    uint8_t ep_row = 0xFF, ep_col = 0xFF;
    uint8_t halfmove = 0;
    uint16_t fullmove = 1;
    int row = 0, col = 0;
    const char *p = fen;

    memset(ui_board, 0, sizeof(ui_board));

    /* Piece placement */
    while (*p && *p != ' ') {
        if (*p == '/') { row++; col = 0; }
        else if (*p >= '1' && *p <= '8') { col += *p - '0'; }
        else {
            int8_t piece = 0;
            switch (*p) {
                case 'P': piece =  1; break; case 'N': piece =  2; break;
                case 'B': piece =  3; break; case 'R': piece =  4; break;
                case 'Q': piece =  5; break; case 'K': piece =  6; break;
                case 'p': piece = -1; break; case 'n': piece = -2; break;
                case 'b': piece = -3; break; case 'r': piece = -4; break;
                case 'q': piece = -5; break; case 'k': piece = -6; break;
            }
            if (piece != 0 && row < 8 && col < 8)
                ui_board[row][col] = piece;
            col++;
        }
        p++;
    }

    if (*p == ' ') p++;
    if (*p == 'b') turn = -1;
    if (*p) p++;
    if (*p == ' ') p++;

    /* Castling */
    if (*p == '-') { p++; }
    else {
        while (*p && *p != ' ') {
            switch (*p) {
                case 'K': castling |= CASTLE_WK; break;
                case 'Q': castling |= CASTLE_WQ; break;
                case 'k': castling |= CASTLE_BK; break;
                case 'q': castling |= CASTLE_BQ; break;
            }
            p++;
        }
    }
    if (*p == ' ') p++;

    /* En passant */
    if (*p != '-' && *p >= 'a' && *p <= 'h') {
        ep_col = *p - 'a'; p++;
        if (*p >= '1' && *p <= '8') { ep_row = 8 - (*p - '0'); p++; }
    } else if (*p) { p++; }
    if (*p == ' ') p++;

    /* Halfmove clock */
    while (*p >= '0' && *p <= '9') { halfmove = halfmove * 10 + (*p - '0'); p++; }
    if (*p == ' ') p++;

    /* Fullmove number */
    while (*p >= '0' && *p <= '9') { fullmove = fullmove * 10 + (*p - '0'); p++; }
    if (fullmove == 0) fullmove = 1;

    board_set_from_ui(b, ui_board, turn, castling, ep_row, ep_col,
                      halfmove, fullmove);
}

/* ========== Screen/Debug Output ========== */

static int line_y = 2;

static void out(const char *s)
{
    gfx_PrintStringXY(s, 2, line_y);
    line_y += 10;
    dbg_printf("%s\n", s);
    gfx_SwapDraw();
    gfx_Blit(gfx_screen);
}

/* ========== Perft ========== */

static uint32_t perft(board_t *b, uint8_t depth)
{
    move_t moves[MAX_MOVES];
    undo_t u;
    uint32_t nodes = 0;
    uint8_t i, count;

    if (depth == 0) return 1;

    count = generate_moves(b, moves, GEN_ALL);

    for (i = 0; i < count; i++) {
        board_make(b, moves[i], &u);
        if (board_is_legal(b))
            nodes += perft(b, depth - 1);
        board_unmake(b, moves[i], &u);
    }

    return nodes;
}

/* ========== Benchmark Positions ========== */

static const char *fens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bqkbnr/1ppp1ppp/p1n5/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 1 4",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R w KQkq - 1 5",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/3PP3/2N2N2/PPP2PPP/R1BQKB1R b KQkq - 0 4",
    "r1bq1rk1/1p4pp/p1n1p3/3n1p2/1b1NQ1P1/2N4P/PPPB1P2/3RKB1R w K - 0 13",
};
#define NUM_POS   5
#define ITERS     1000

/* ========== Main ========== */

int main(void)
{
    board_t b;
    move_t moves[256];
    undo_t undo;
    char buf[50];
    uint32_t cycles, total_cycles, ms, nodes;
    uint8_t nmoves;
    int i, j;
    int16_t eval_result;
    search_limits_t limits;
    search_result_t sr;
    engine_hooks_t hooks;

    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_ZeroScreen();
    gfx_SetTextFGColor(255);

    /* Enable hardware timer 1: 48 MHz CPU clock, count up */
    timer_Enable(1, TIMER_CPU, TIMER_NOINT, TIMER_UP);

    out("=== Chess Engine Benchmark ===");

    /* Init engine internals */
    zobrist_init(0x12345678);
    search_init();
    tt_clear();
    hooks.time_ms = bench_time_ms;
    engine_init(&hooks);

    /* ======== 1. Memory Sizes ======== */
    out("-- Memory --");
    sprintf(buf, "board_t: %u B", (unsigned)sizeof(board_t));
    out(buf);
    sprintf(buf, "undo_t:  %u B", (unsigned)sizeof(undo_t));
    out(buf);
    sprintf(buf, "move_t:  %u B", (unsigned)sizeof(move_t));
    out(buf);
    dbg_printf("zobrist tables = %u bytes\n",
               (unsigned)(sizeof(zobrist_piece) + sizeof(zobrist_castle) +
                          sizeof(zobrist_ep_file) + sizeof(zobrist_side) +
                          sizeof(lock_piece) + sizeof(lock_castle) +
                          sizeof(lock_ep_file) + sizeof(lock_side)));
    dbg_printf("perft frame ~%u bytes (moves[%u] + undo + locals)\n",
               (unsigned)(sizeof(move_t) * MAX_MOVES + sizeof(undo_t) + 32),
               (unsigned)MAX_MOVES);

    /* ======== 2. Single-Call Operation Timing ======== */
    out("-- Single Ops (startpos) --");
    parse_fen_board(fens[0], &b);

    /* generate_moves */
    timer_Set(1, 0);
    nmoves = generate_moves(&b, moves, GEN_ALL);
    cycles = timer_GetSafe(1, TIMER_UP);
    sprintf(buf, "movegen: %u moves %lu cy", nmoves, (unsigned long)cycles);
    out(buf);

    /* is_square_attacked */
    timer_Set(1, 0);
    (void)is_square_attacked(&b, SQ_E1, BLACK);
    cycles = timer_GetSafe(1, TIMER_UP);
    sprintf(buf, "attacked(e1): %lu cy", (unsigned long)cycles);
    out(buf);

    /* make+unmake averaged over all startpos moves */
    {
        uint32_t total = 0;
        nmoves = generate_moves(&b, moves, GEN_ALL);
        for (i = 0; i < nmoves; i++) {
            timer_Set(1, 0);
            board_make(&b, moves[i], &undo);
            board_unmake(&b, moves[i], &undo);
            total += timer_GetSafe(1, TIMER_UP);
        }
        sprintf(buf, "mk/unmk: %lu avg cy", (unsigned long)(total / nmoves));
        out(buf);
    }

    /* evaluate */
    timer_Set(1, 0);
    eval_result = evaluate(&b);
    cycles = timer_GetSafe(1, TIMER_UP);
    sprintf(buf, "eval: %d  %lu cy", eval_result, (unsigned long)cycles);
    out(buf);

    /* ======== 3. Iterated Component Benchmarks ======== */
    out("-- Movegen x1000 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        timer_Set(1, 0);
        for (j = 0; j < ITERS; j++)
            nmoves = generate_moves(&b, moves, 0);
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy/call", i, (unsigned long)(cycles / ITERS));
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/call",
            (unsigned long)(total_cycles / (NUM_POS * ITERS)));
    out(buf);

    out("-- Eval x1000 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        timer_Set(1, 0);
        for (j = 0; j < ITERS; j++)
            eval_result = evaluate(&b);
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy/call", i, (unsigned long)(cycles / ITERS));
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/call",
            (unsigned long)(total_cycles / (NUM_POS * ITERS)));
    out(buf);
    (void)eval_result;

    out("-- Make/Unmake x1000 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        nmoves = generate_moves(&b, moves, 0);
        if (nmoves == 0) continue;
        timer_Set(1, 0);
        for (j = 0; j < ITERS; j++) {
            board_make(&b, moves[0], &undo);
            board_unmake(&b, moves[0], &undo);
        }
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy/pair", i, (unsigned long)(cycles / ITERS));
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/pair",
            (unsigned long)(total_cycles / (NUM_POS * ITERS)));
    out(buf);

    /* ======== 4. Perft ======== */
    out("-- Perft (startpos) --");
    for (j = 1; j <= 5; j++) {
        parse_fen_board(fens[0], &b);
        timer_Set(1, 0);
        nodes = perft(&b, (uint8_t)j);
        cycles = timer_GetSafe(1, TIMER_UP);
        ms = cycles / 48000UL;
        sprintf(buf, "d%d: %lu n  %lu ms", j,
                (unsigned long)nodes, (unsigned long)ms);
        out(buf);
        dbg_printf("  perft(%d) = %lu nodes  %lu cycles",
                   j, (unsigned long)nodes, (unsigned long)cycles);
        if (ms > 0)
            dbg_printf("  %lu knps", (unsigned long)(nodes / ms));
        dbg_printf("\n");
    }

    /* ======== 5. Search Benchmarks ======== */
    out("-- Search d3 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        search_history_clear();
        tt_clear();
        limits.max_depth = 3;
        limits.max_time_ms = 0;
        limits.max_nodes = 0;
        limits.time_fn = NULL;
        timer_Set(1, 0);
        sr = search_go(&b, &limits);
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy n=%lu",
                i, (unsigned long)cycles, (unsigned long)sr.nodes);
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/search",
            (unsigned long)(total_cycles / NUM_POS));
    out(buf);

    out("-- Search d4 --");
    total_cycles = 0;
    for (i = 0; i < NUM_POS; i++) {
        parse_fen_board(fens[i], &b);
        search_history_clear();
        tt_clear();
        limits.max_depth = 4;
        limits.max_time_ms = 0;
        limits.max_nodes = 0;
        limits.time_fn = NULL;
        timer_Set(1, 0);
        sr = search_go(&b, &limits);
        cycles = timer_GetSafe(1, TIMER_UP);
        total_cycles += cycles;
        sprintf(buf, "P%d: %lu cy n=%lu",
                i, (unsigned long)cycles, (unsigned long)sr.nodes);
        out(buf);
    }
    sprintf(buf, "Avg: %lu cy/search",
            (unsigned long)(total_cycles / NUM_POS));
    out(buf);

    out("=== Done ===");

    timer_Disable(1);

    /* Signal termination to emulator */
    *(volatile uint8_t *)0xFB0000 = 0;

    for (;;) ;

    return 0;
}
