# Gödel's Fugue

```
cmake -S . -B build
cmake --build build
./build/canon-collapse
./build/canon-collapse --config examples/inversion.txt
./build/canon-collapse --config examples/retrograde.txt
./build/canon-collapse --config examples/energy.txt
./build/canon-collapse --config examples/three_voice.txt
./build/canon-collapse --config examples/transpose.txt
./build/canon-collapse --config examples/augment.txt
./build/canon-collapse --config examples/diminish.txt
./build/canon-collapse --config examples/phase.txt
./build/canon-collapse --config examples/stagger.txt
./build/canon-collapse --lock 0 61
./build/canon-collapse --config examples/lock.txt
./build/canon-collapse --config examples/anneal.txt
./build/canon-collapse --config examples/dissonance.txt
./build/canon-collapse --config examples/cadence.txt
./build/canon-collapse --config examples/rest.txt
./build/canon-collapse --config examples/cyclic.txt
./build/canon-collapse --config examples/motif.txt
./build/canon-collapse --config examples/unsat.txt
```

A successful run also writes `output/score.musicxml`, `output/contour.svg`, `output/voices.wav`, `output/proof.json`, `output/score.html`, and `output/report.txt` beside the MIDI and proof files. Open `output/score.html` as a file. An unsatisfiable rule set prints `core:` on stderr.
