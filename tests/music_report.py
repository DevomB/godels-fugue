"""Before-and-after report on the music, for people to read and to listen to.

Runs two builds of godels-fugue (say the last release and this change) over
the presets and several variations, and measures in each melody what a
listener hears as identity and development:

  returns   later bars that bring back the opening bar's rhythm and contour
            (75% of its steps or more), as it is or upside down (the form
            inverts a return that sounds against another voice's subject),
            out of the bars after the first
  echo      the best such match, in percent
  contrast  attacks in the busiest bar minus the calmest, per beat
  breaths   long notes (two beats or more) and rests inside the melody
  repeats   later bars identical to an earlier bar in intervals and rhythm
            (high means mechanical)
  peak      where the highest note first sounds, as a share of the melody,
            and how many times it sounds
  end       the final note's length in beats
  step      share of intervals of a whole step or less
  form      what the form's terms cost: subject/contrast/breath/arrival

It writes a Markdown table and, for listening, per preset and variation:
  composition/<before|after>.mid (and .wav for the first variation): both
  builds, the same synth and the plain (unshaped) performance, so only the
  notes differ;
  expression/plain and <mood> .mid (and .wav for the first variation): this
  build's same notes played plain and in the preset's mood, so only the
  performance differs; score.html, the piece in the score page.

usage: python3 tests/music_report.py BEFORE_EXE AFTER_EXE OUT_DIR [--check]
                                    [--variant NAME=KEY=VALUE,KEY=VALUE ...]
--check fails when an after piece with a form has no return of its subject.
--variant also measures the after build with those settings added, to weigh a
change of weights before making it.
"""
import json
import os
import shutil
import subprocess
import sys

PRESETS = ["lament", "hymn", "triumph", "longing", "dance", "nocturne"]
SEEDS = [0, 7, 23]


def run(exe, out, args):
    os.makedirs(out, exist_ok=True)
    cmd = [exe, "--out", os.path.join(out, "canon.mid"), "--proof", os.path.join(out, "proof.txt"),
           "--entropy", os.path.join(out, "entropy.txt")] + args
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if p.returncode != 0:
        return None, (p.stdout + p.stderr).strip().splitlines()[-1:] or ["exit %d" % p.returncode]
    with open(os.path.join(out, "proof.json"), encoding="utf-8") as f:
        return json.load(f), None


def melody_steps(proof):
    """Pitch and attack per melody step of voice 1, which plays the melody."""
    length = int(proof["config"]["length"])
    pitch = [0] * length
    attack = [False] * length
    for n in proof["score"][0]:
        for s in range(n["start"], min(length, n["start"] + n["length"])):
            pitch[s] = max(n["pitch"], 0)
            attack[s] = s == n["start"]
    return pitch, attack


def sign(x):
    return (x > 0) - (x < 0)


def measure(proof):
    spb = proof["stepsPerBeat"]
    bar = 4 * spb
    pitch, attack = melody_steps(proof)
    length = len(pitch)
    bars = length // bar

    def bar_shape(b):
        out = []
        for j in range(1, bar):
            i = b * bar + j
            if i >= length:
                break
            move = 0 if pitch[i] == 0 or pitch[i - 1] == 0 else sign(pitch[i] - pitch[i - 1])
            out.append((attack[i], pitch[i] == 0, move))
        return out

    head = bar_shape(0)
    upside = [(a, r, -m) for a, r, m in head]
    matches = []
    for b in range(1, bars):
        other = bar_shape(b)
        n = min(len(head), len(other))
        if n == 0:
            continue
        same = sum(1 for k in range(n) if head[k] == other[k])
        flipped = sum(1 for k in range(n) if upside[k] == other[k])
        matches.append(max(same, flipped) / n)
    returns = sum(1 for m in matches if m >= 0.75)

    def intervals(b):
        seg = pitch[b * bar:(b + 1) * bar]
        att = attack[b * bar:(b + 1) * bar]
        return tuple(att), tuple(seg[k] - seg[k - 1] for k in range(1, len(seg)))

    seen = set()
    repeats = 0
    for b in range(bars):
        key = intervals(b)
        if key in seen:
            repeats += 1
        seen.add(key)

    activity = [sum(1 for i in range(b * bar, (b + 1) * bar) if attack[i] and pitch[i]) / 4.0
                for b in range(bars)]
    notes = [n for n in proof["score"][0] if n["start"] < length]
    breaths = sum(1 for n in notes if n["pitch"] <= 0 or n["length"] >= 2 * spb) - 1  # not the last
    sounding = [n for n in notes if n["pitch"] > 0]
    top = max(n["pitch"] for n in sounding)
    peak_at = min(n["start"] for n in sounding if n["pitch"] == top) / max(1, length - 1)
    peaks = sum(1 for n in sounding if n["pitch"] == top)
    ps = [n["pitch"] for n in sounding]
    moves = [b - a for a, b in zip(ps, ps[1:]) if b != a]
    return {
        "returns": "%d/%d" % (returns, max(0, bars - 1)),
        "returns_n": returns,
        "echo": int(round(100 * max(matches))) if matches else 0,
        "contrast": round(max(activity) - min(activity), 2) if activity else 0,
        "breaths": max(0, breaths),
        "repeats": repeats,
        "peak": "%.2f x%d" % (peak_at, peaks),
        "end": round(sounding[-1]["length"] / spb, 2),
        "step": int(round(100 * sum(1 for m in moves if abs(m) <= 2) / max(1, len(moves)))),
        "phrases": len(proof.get("form", {}).get("phrases", [])) or 1,
        "ms": int(round(1000 * proof["stats"]["seconds"])),
        "form": "/".join(str(proof["energy"]["parts"].get(k, 0)) for k in ("subject", "contrast", "breath", "arrival")),
        "energy": proof["energy"]["total"],
        "top": " ".join("%s %d" % kv for kv in sorted(proof["energy"]["parts"].items(),
                                                       key=lambda kv: -kv[1])[:3]),
    }


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    before, after, out = sys.argv[1], sys.argv[2], sys.argv[3]
    check = "--check" in sys.argv
    variants = []
    for i, a in enumerate(sys.argv):
        if a == "--variant" and i + 1 < len(sys.argv):
            name, _, sets = sys.argv[i + 1].partition("=")
            variants.append((name, [x for kv in sets.split(",") if kv for x in ("--set", kv)]))
    rows = []
    problems = []
    for preset in PRESETS:
        for seed in SEEDS:
            var = [] if seed == 0 else ["--set", "seed=%d" % seed, "--set", "temperature=3"]
            here = os.path.join(out, "%s-v%d" % (preset, seed))
            same_sound = ["--set", "mood=plain", "--set", "instrument=pluck"]
            got = {}
            for name, exe in (("before", before), ("after", after)):
                proof, err = run(exe, os.path.join(here, "work-" + name),
                                 ["--preset", preset] + var + same_sound)
                got[name] = proof
                if proof is None:
                    rows.append((preset, seed, name, None, err))
                    continue
                os.makedirs(os.path.join(here, "composition"), exist_ok=True)
                # recordings for the first variation; MIDI, which is small, for all
                for ext, src in ((("wav", "voices.wav"),) if seed == SEEDS[0] else ()) + (("mid", "canon.mid"),):
                    shutil.copy(os.path.join(here, "work-" + name, src),
                                os.path.join(here, "composition", "%s.%s" % (name, ext)))
                rows.append((preset, seed, name, measure(proof), None))
            for vname, vsets in variants:
                proof, err = run(after, os.path.join(here, "work-" + vname),
                                 ["--preset", preset] + var + same_sound + vsets)
                rows.append((preset, seed, vname, measure(proof) if proof else None, err))
                shutil.rmtree(os.path.join(here, "work-" + vname), ignore_errors=True)
            # the same notes, plain and in the preset's own mood
            proof, err = run(after, os.path.join(here, "work-mood"), ["--preset", preset] + var)
            if proof is not None:
                mood = proof["perform"]["mood"]
                os.makedirs(os.path.join(here, "expression"), exist_ok=True)
                if seed == SEEDS[0]:
                    shutil.copy(os.path.join(here, "work-after", "voices.wav"),
                                os.path.join(here, "expression", "plain.wav"))
                    shutil.copy(os.path.join(here, "work-mood", "voices.wav"),
                                os.path.join(here, "expression", "%s.wav" % mood))
                shutil.copy(os.path.join(here, "work-after", "canon.mid"),
                            os.path.join(here, "expression", "plain.mid"))
                shutil.copy(os.path.join(here, "work-mood", "canon.mid"),
                            os.path.join(here, "expression", "%s.mid" % mood))
                shutil.copy(os.path.join(here, "work-mood", "score.html"),
                            os.path.join(here, "score.html"))
                notes = lambda q: [[(n["start"], n["length"], n["pitch"]) for n in v] for v in q["score"]]
                if got["after"] is not None and notes(proof) != notes(got["after"]):
                    problems.append("%s v%d: the mood changed the notes" % (preset, seed))
            for w in ("work-before", "work-after", "work-mood"):
                shutil.rmtree(os.path.join(here, w), ignore_errors=True)
            a = got["after"]
            if check and a is not None:
                m = measure(a)
                if m["phrases"] > 1 and m["returns_n"] == 0:
                    problems.append("%s v%d: the subject never returns" % (preset, seed))

    cols = ["ms", "energy", "top", "form", "phrases", "returns", "echo", "contrast", "breaths", "repeats", "peak", "end", "step"]
    lines = ["| preset | var | build | " + " | ".join(cols) + " |",
             "| --- | --- | --- | " + " | ".join("---" for _ in cols) + " |"]
    for preset, seed, name, m, err in rows:
        if m is None:
            lines.append("| %s | %d | %s | %s |" % (preset, seed, name, "failed: " + " ".join(err)))
        else:
            lines.append("| %s | %d | %s | %s |" % (preset, seed, name, " | ".join(str(m[c]) for c in cols)))
    report = "\n".join(lines) + "\n"
    with open(os.path.join(out, "report.md"), "w", encoding="utf-8") as f:
        f.write(__doc__.split("usage:")[0])
        f.write("\n" + report)
        if problems:
            f.write("\nProblems:\n" + "\n".join("- " + p for p in problems) + "\n")
    print(report)
    for p in problems:
        print("PROBLEM " + p)
    return 1 if check and problems else 0


if __name__ == "__main__":
    sys.exit(main())
