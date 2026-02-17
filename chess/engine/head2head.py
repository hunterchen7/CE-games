#!/usr/bin/env python3
"""
Head-to-head: book engine vs no-book engine, 200 games, high concurrency.
"""

import os
import sys
import math
import datetime
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
import chess
import chess.engine
import chess.pgn

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(BASE_DIR, "build")
ENGINE = os.path.join(BUILD_DIR, "uci_6000")
BOOK_PATH = os.path.join(BASE_DIR, "..", "books", "book_large.bin")

TOTAL_GAMES = 200
MOVETIME = 0.1
MAX_WORKERS = 20  # high concurrency since both are local engines

print_lock = threading.Lock()
pgn_lock = threading.Lock()

stats_lock = threading.Lock()
wins_book = 0
draws = 0
wins_nobook = 0
games_done = 0


def log(msg):
    with print_lock:
        print(msg, flush=True)


def play_game(game_num, book_is_white):
    global wins_book, draws, wins_nobook, games_done

    board = chess.Board()
    game = chess.pgn.Game()
    game.headers["Event"] = "Book vs No-Book H2H"
    game.headers["Date"] = datetime.date.today().strftime("%Y.%m.%d")
    game.headers["Round"] = str(game_num + 1)

    book_eng = chess.engine.SimpleEngine.popen_uci(
        [ENGINE, "-book", BOOK_PATH])
    nobook_eng = chess.engine.SimpleEngine.popen_uci([ENGINE])

    if book_is_white:
        w_eng, b_eng = book_eng, nobook_eng
        game.headers["White"] = "Book"
        game.headers["Black"] = "NoBook"
    else:
        w_eng, b_eng = nobook_eng, book_eng
        game.headers["White"] = "NoBook"
        game.headers["Black"] = "Book"

    limit = chess.engine.Limit(time=MOVETIME)
    node = game

    try:
        while not board.is_game_over(claim_draw=True) and board.fullmove_number <= 200:
            eng = w_eng if board.turn == chess.WHITE else b_eng
            try:
                result = eng.play(board, limit)
            except chess.engine.EngineTerminatedError:
                game.headers["Result"] = "0-1" if board.turn == chess.WHITE else "1-0"
                break
            if result.move is None:
                break
            node = node.add_variation(result.move)
            board.push(result.move)
        else:
            outcome = board.outcome(claim_draw=True)
            game.headers["Result"] = outcome.result() if outcome else "1/2-1/2"
    finally:
        for e in [w_eng, b_eng]:
            try:
                e.quit()
            except Exception:
                pass

    # Score from book engine's perspective
    res = game.headers["Result"]
    if book_is_white:
        sc = 1.0 if res == "1-0" else (0.0 if res == "0-1" else 0.5)
    else:
        sc = 1.0 if res == "0-1" else (0.0 if res == "1-0" else 0.5)

    with stats_lock:
        if sc == 1.0:
            wins_book += 1
        elif sc == 0.5:
            draws += 1
        else:
            wins_nobook += 1
        games_done += 1
        done = games_done
        wb, d, wnb = wins_book, draws, wins_nobook

    if done % 10 == 0 or done == TOTAL_GAMES:
        total_sc = wb + 0.5 * d
        pct = total_sc / done * 100
        log(f"  [{done}/{TOTAL_GAMES}] Book +{wb} ={d} -{wnb}  "
            f"Score: {total_sc:.1f}/{done} ({pct:.1f}%)")

    return game


def main():
    if not os.path.isfile(ENGINE):
        print(f"Missing engine: {ENGINE}")
        sys.exit(1)
    if not os.path.isfile(BOOK_PATH):
        print(f"Missing book: {BOOK_PATH}")
        sys.exit(1)

    pgn_path = os.path.join(BASE_DIR, "h2h_book_vs_nobook.pgn")
    with open(pgn_path, "w"):
        pass

    print(f"Head-to-Head: Book (large) vs No-Book")
    print(f"Engine: uci_6000 (6000 nodes, 0.1s/move)")
    print(f"{TOTAL_GAMES} games ({TOTAL_GAMES//2} as white, {TOTAL_GAMES//2} as black)")
    print(f"Concurrency: {MAX_WORKERS} workers")
    print()

    # Build game schedule: first half book=white, second half book=black
    schedule = []
    half = TOTAL_GAMES // 2
    for i in range(half):
        schedule.append((i, True))   # book is white
    for i in range(half):
        schedule.append((half + i, False))  # book is black

    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futures = {pool.submit(play_game, num, bw): num
                   for num, bw in schedule}
        for fut in as_completed(futures):
            try:
                game = fut.result()
                with pgn_lock:
                    with open(pgn_path, "a") as f:
                        print(game, file=f)
                        print(file=f)
            except Exception as ex:
                num = futures[fut]
                log(f"  ERROR game {num}: {ex}")

    total_sc = wins_book + 0.5 * draws
    pct = total_sc / TOTAL_GAMES
    if 0 < pct < 1:
        elo = -400 * math.log10((1 - pct) / pct)
        elo_str = f"{elo:+.0f}"
    elif pct >= 1:
        elo_str = "+inf"
    else:
        elo_str = "-inf"

    print(f"\n{'='*60}")
    print(f"{'RESULTS — Book vs No-Book (uci_6000, 0.1s)':^60}")
    print(f"{'='*60}")
    print(f"  Book wins:    {wins_book}")
    print(f"  Draws:        {draws}")
    print(f"  No-book wins: {wins_nobook}")
    print(f"  Score:        {total_sc:.1f}/{TOTAL_GAMES} ({pct*100:.1f}%)")
    print(f"  Elo diff:     {elo_str} (book relative to no-book)")
    print(f"{'='*60}")
    print(f"\nPGN saved to: {pgn_path}")


if __name__ == "__main__":
    main()
