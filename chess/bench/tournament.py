#!/usr/bin/env python3
"""
TI84Chess vs Stockfish at various UCI_Elo levels (1320-2420, 100 increments).
2 games per level (swap colors). Games saved incrementally to tournament.pgn.
"""

import os
import sys
import datetime
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
import chess
import chess.engine
import chess.pgn

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
OUR_ENGINE = os.path.join(BASE_DIR, "chess", "engine", "build", "uci")
STOCKFISH = "/opt/homebrew/bin/stockfish"
MOVETIME = 0.1  # seconds per move
GAMES_PER_LEVEL = 20  # 10 as white, 10 as black

ELO_LEVELS = list(range(1320, 2520, 100))  # 1320, 1420, ..., 2420

MAX_WORKERS = 12

print_lock = threading.Lock()
pgn_lock = threading.Lock()


def log(msg):
    with print_lock:
        print(msg, flush=True)


def make_participant(name, path, uci_opts=None):
    return {"name": name, "path": path, "opts": uci_opts or {}}


def play_game(white, black):
    """Play one game. Returns chess.pgn.Game."""
    board = chess.Board()
    game = chess.pgn.Game()
    game.headers["Event"] = "TI84Chess vs Stockfish"
    game.headers["Date"] = datetime.date.today().strftime("%Y.%m.%d")
    game.headers["White"] = white["name"]
    game.headers["Black"] = black["name"]

    w_eng = chess.engine.SimpleEngine.popen_uci(white["path"])
    b_eng = chess.engine.SimpleEngine.popen_uci(black["path"])

    try:
        if white["opts"]:
            w_eng.configure(white["opts"])
        if black["opts"]:
            b_eng.configure(black["opts"])
    except Exception as e:
        log(f"  Config error: {e}")
        w_eng.quit()
        b_eng.quit()
        game.headers["Result"] = "*"
        return game

    limit = chess.engine.Limit(time=MOVETIME)
    node = game

    try:
        while not board.is_game_over(claim_draw=True) and board.fullmove_number <= 200:
            eng = w_eng if board.turn == chess.WHITE else b_eng
            try:
                result = eng.play(board, limit)
            except chess.engine.EngineTerminatedError:
                game.headers["Result"] = "0-1" if board.turn == chess.WHITE else "1-0"
                return game
            if result.move is None:
                break
            node = node.add_variation(result.move)
            board.push(result.move)

        outcome = board.outcome(claim_draw=True)
        if outcome:
            game.headers["Result"] = outcome.result()
        else:
            game.headers["Result"] = "1/2-1/2"
    finally:
        try:
            w_eng.quit()
        except Exception:
            pass
        try:
            b_eng.quit()
        except Exception:
            pass

    return game


def result_score(result_str, for_white):
    if result_str == "1-0":
        return 1.0 if for_white else 0.0
    elif result_str == "0-1":
        return 0.0 if for_white else 1.0
    elif result_str == "1/2-1/2":
        return 0.5
    return 0.0


def main():
    if not os.path.isfile(OUR_ENGINE):
        print(f"Engine not found: {OUR_ENGINE}")
        print("Build: cd chess/engine && make uci")
        sys.exit(1)

    ti = make_participant("TI84Chess", OUR_ENGINE)
    opponents = []
    for elo in ELO_LEVELS:
        opponents.append(make_participant(
            f"SF-{elo}", STOCKFISH,
            {"Threads": 1, "UCI_LimitStrength": True, "UCI_Elo": elo}
        ))

    total_matches = len(opponents)
    games_total = total_matches * GAMES_PER_LEVEL
    print(f"TI84Chess vs Stockfish: {total_matches} levels, {GAMES_PER_LEVEL} games each ({games_total} total)")
    print(f"Elo levels: {ELO_LEVELS[0]}-{ELO_LEVELS[-1]}")
    print(f"Time control: {MOVETIME}s/move, max {MAX_WORKERS} concurrent")
    print()

    pgn_path = os.path.join(BASE_DIR, "tournament.pgn")
    with open(pgn_path, "w") as f:
        pass

    all_games = []
    results = {}  # name -> {"w": wins, "d": draws, "l": losses, "score": float}
    results_lock = threading.Lock()

    def append_game(game):
        with pgn_lock:
            with open(pgn_path, "a") as f:
                print(game, file=f)
                print(file=f)

    def run_match(opp):
        half = GAMES_PER_LEVEL // 2
        games = []
        log(f"  Starting: TI84Chess vs {opp['name']} ({GAMES_PER_LEVEL} games)")

        for i in range(GAMES_PER_LEVEL):
            if i < half:
                g = play_game(ti, opp)   # TI84Chess as white
                sc = result_score(g.headers["Result"], True)
            else:
                g = play_game(opp, ti)   # TI84Chess as black
                sc = result_score(g.headers["Result"], False)
            games.append((g, sc))

        ti_score = sum(sc for _, sc in games)
        wins = sum(1 for _, sc in games if sc == 1.0)
        draws = sum(1 for _, sc in games if sc == 0.5)
        losses = sum(1 for _, sc in games if sc == 0.0)

        log(f"  Done: vs {opp['name']}  +{wins}={draws}-{losses}  "
            f"TI84Chess {ti_score:.1f}/{GAMES_PER_LEVEL}")

        with results_lock:
            for g, _ in games:
                all_games.append(g)
                append_game(g)
            results[opp['name']] = {
                "w": wins, "d": draws, "l": losses, "score": ti_score
            }

    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futures = {pool.submit(run_match, opp): opp for opp in opponents}
        done = 0
        for f in as_completed(futures):
            done += 1
            try:
                f.result()
            except Exception as e:
                opp = futures[f]
                log(f"  ERROR vs {opp['name']}: {e}")
            if done % 4 == 0 or done == total_matches:
                log(f"  Progress: {done}/{total_matches} levels complete")

    print(f"\nPGNs saved to: {pgn_path} ({len(all_games)} games)")

    # Results table
    total_score = 0.0
    print(f"\n{'='*60}")
    print(f"{'TI84Chess RESULTS':^60}")
    print(f"{'='*60}")
    print(f"{'Opponent':<12} {'W':>4} {'D':>4} {'L':>4} {'Score':>8} {'/ '+str(GAMES_PER_LEVEL):>5} {'%':>7}")
    print(f"{'-'*60}")
    for elo in ELO_LEVELS:
        name = f"SF-{elo}"
        if name in results:
            r = results[name]
            total_score += r["score"]
            pct = r["score"] / GAMES_PER_LEVEL * 100
            print(f"  {name:<10} {r['w']:>4} {r['d']:>4} {r['l']:>4} "
                  f"{r['score']:>6.1f}  /{GAMES_PER_LEVEL}  {pct:>5.1f}%")
    print(f"{'-'*60}")
    max_score = games_total
    pct = total_score / max_score * 100 if max_score else 0
    print(f"  {'TOTAL':<10} {'':>4} {'':>4} {'':>4} "
          f"{total_score:>6.1f}  /{max_score}  {pct:>5.1f}%")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
