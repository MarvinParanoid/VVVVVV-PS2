#!/usr/bin/env python3
"""Compare scene-benchmark logs (vvvvvv_ps2.log from BENCH runs) as markdown.

    ps2-bench-compare.py <baseline.log> <candidate.log> [--breakdown]

Per scene: logic rate (ticks/s, the game's speed; 29.4 = full speed), loop
period distribution (what the player sees), render time, and with
--breakdown where a rendered frame's time goes. Also checks that golden
frames (pass 0 of the benchmark) are identical, which they must be when only
optimisations differ.
"""
import re
import sys

NUM = r"([-\d.]+)"
LINE = re.compile(
    r"BENCH scene=(\S+) frames=\d+ renders=\d+ secs=[\d.]+ \| logic NUM ticks/s, render NUM fps, multi-tick loops \d+ \| "
    r"period mean=NUM p50=NUM p95=NUM p99=NUM max=NUM ms \| render mean=NUM p50=NUM p95=NUM p99=NUM max=NUM ms \| "
    r"per render: logic-in-loop=NUM draw=NUM sw=NUM gs_upload=NUM gs_dma=NUM vsync=NUM ms \| logic per tick=NUM ms \| "
    r"cmds/render=NUM copies=NUM fills=NUM \| music decode NUM ms/s(?: \| idle per loop NUM ms)?".replace("NUM", NUM))
KEYS = ["ticks", "fps", "p_mean", "p_p50", "p_p95", "p_p99", "p_max", "r_mean", "r_p50", "r_p95", "r_p99", "r_max",
        "logic_loop", "draw", "sw", "gs_up", "gs_dma", "vsync", "logic_tick", "cmds", "copies", "fills", "decode", "idle"]


def load(path):
    scenes, golden, underruns = {}, {}, {}
    for line in open(path, errors="replace"):
        m = LINE.search(line)
        if m:
            vals = [float(x) if x is not None else 0.0 for x in m.groups()[1:]]
            scenes[m.group(1)] = dict(zip(KEYS, vals))
        m = re.search(r"GOLDEN scene=(\S+) ticks=\d+ crc32=(\w+)(?: window_crc32=(\w+))?", line)
        if m:
            golden[m.group(1)] = (m.group(2), m.group(3))
        m = re.search(r"BENCH scene=(\S+) music: .*underruns during scene=(\d+)", line)
        if m:
            underruns[m.group(1)] = int(m.group(2))
    return scenes, golden, underruns


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        sys.exit(__doc__)
    (a, ga, ua), (b, gb, ub) = load(args[0]), load(args[1])
    print("| scene | ticks/s | period mean | period p95 | period p99 | period max | render mean | render p95 | logic/tick | underruns |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for s in a:
        if s not in b:
            continue
        x, y = a[s], b[s]
        f = lambda k: f"{x[k]:.1f} → **{y[k]:.1f}**"
        print(f"| {s} | {f('ticks')} | {f('p_mean')} | {f('p_p95')} | {f('p_p99')} | {f('p_max')} | {f('r_mean')} | "
              f"{f('r_p95')} | {f('logic_tick')} | {ua.get(s, '-')} → {ub.get(s, '-')} |")
    if "--breakdown" in sys.argv:
        print()
        print("| scene | VVVVVV draw + SDL queueing | SDL software rasterisation | GS upload | GS DMA/finish | logic in loop | idle per loop | music decode (ms/s) | cmds/frame |")
        print("|---|---|---|---|---|---|---|---|---|")
        for s in a:
            if s not in b:
                continue
            x, y = a[s], b[s]
            f = lambda k: f"{x[k]:.2f} → {y[k]:.2f}"
            print(f"| {s} | {f('draw')} | {f('sw')} | {f('gs_up')} | {f('gs_dma')} | {f('logic_loop')} | {f('idle')} | "
                  f"{x['decode']:.0f} → {y['decode']:.0f} | {x['cmds']:.0f} → {y['cmds']:.0f} |")
    same = [s for s in ga if s in gb and ga[s] == gb[s]]
    diff = [s for s in ga if s in gb and ga[s] != gb[s]]
    print()
    print(f"golden frames identical: {len(same)}/{len(same) + len(diff)}" + (f"; DIFFERENT: {', '.join(diff)}" if diff else ""))


if __name__ == "__main__":
    main()
