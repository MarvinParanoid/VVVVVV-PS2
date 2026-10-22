#!/usr/bin/env python3
"""Summarise a vvvvvv_ps2.log (PS2Platform instrumentation) from a PCSX2 run.

    summarize-ps2-log.py <vvvvvv_ps2.log> [--all-states]

FRAME lines are 300-loop windows (mean/p50/p95/max per window), so the run
P95 reported here is the distribution of window P95s (median and worst), not a
per-frame percentile. By default only gameplay windows (gamestate=0) count.
"""
import re
import sys

FRAME = re.compile(r"^FRAME loops=(\d+) period mean=(\d+) max=(\d+) us \| work\(logic\+render\) mean=(\d+) max=(\d+) us \| "
                   r"render mean=(\d+) p50=(\d+) p95=(\d+) max=(\d+) us \((\d+) renders\) \| gamestate=(-?\d+)")
MEM = re.compile(r"^MEM .*?malloc_in_use=(\d+) arena=(\d+) headroom=(\d+)")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    all_states = "--all-states" in sys.argv
    wins, mem, music = [], [], None
    for line in open(sys.argv[1], errors="replace"):
        m = FRAME.match(line)
        if m:
            v = list(map(int, m.groups()))
            if all_states or v[10] == 0:
                wins.append(v)
            continue
        m = MEM.match(line)
        if m:
            mem.append(tuple(map(int, m.groups())))
        elif line.startswith("MUSIC active"):
            music = line.strip()
    if not wins:
        sys.exit("no matching FRAME windows")
    loops = sum(w[0] for w in wins)
    renders = sum(w[9] for w in wins)
    mean = lambda i, n: sum(w[i] * w[n] for w in wins) / sum(w[n] for w in wins)
    p95s = sorted(w[7] for w in wins)
    print(f"windows={len(wins)} loops={loops} renders={renders}")
    print(f"period mean={mean(1, 0) / 1000:.2f} ms max={max(w[2] for w in wins) / 1000:.2f} ms "
          f"(>34 ms windows-max: {sum(w[2] > 34000 for w in wins)})")
    print(f"work   mean={mean(3, 0) / 1000:.2f} ms max={max(w[4] for w in wins) / 1000:.2f} ms")
    print(f"render mean={mean(5, 9) / 1000:.2f} ms p95 median-window={p95s[len(p95s) // 2] / 1000:.2f} "
          f"worst-window={p95s[-1] / 1000:.2f} ms max={max(w[8] for w in wins) / 1000:.2f} ms")
    if mem:
        print(f"memory peak malloc_in_use={max(x[0] for x in mem) / 1048576:.2f} MiB "
              f"arena={max(x[1] for x in mem) / 1048576:.2f} MiB min headroom={min(x[2] for x in mem) / 1048576:.2f} MiB "
              f"({len(mem)} samples)")
    if music:
        print(music)


if __name__ == "__main__":
    main()
