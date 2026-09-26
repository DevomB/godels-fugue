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
./build/canon-collapse --config examples/anneal_geo.txt
./build/canon-collapse --config examples/invert_mod12.txt
./build/canon-collapse --config examples/modulate.txt
./build/canon-collapse --config examples/poly.txt
./build/canon-collapse --corpus corpus
./build/canon-collapse --corpus corpus --apply-weights
./build/canon-collapse --sat
```

New config keys: `anneal_ratio`, `invert_mod12`, `modulate_at`, `key_second`, `w_modulate`, `poly_meter`, `motif_c`, `motif_d`, `sample`. CLI: `--corpus DIR`, `--apply-weights`, `--sat`. `output/score.html` filters proof events by variable id.

A successful run also writes `output/score.musicxml`, `output/contour.svg`, `output/voices.wav`, `output/proof.json`, `output/score.html`, and `output/report.txt` beside the MIDI and proof files. Open `output/score.html` as a file. An unsatisfiable rule set prints `core:` on stderr.
