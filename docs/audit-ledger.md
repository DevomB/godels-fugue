# Audit ledger

One row per issue: the evidence, its kind (**bug** confirmed by reading or a
failing check, **limit** a design limitation, **hyp** a hypothesis), its priority
(P1 correctness or data loss, P2 state and usability, P3 musical quality, P4
performance and upkeep), the shared cause, the fix, and how it is checked.
Checks run in GitHub Actions on every push: the C tests on Linux (GCC, Clang, ASan
and UBSan), macOS and Windows (MSVC `/W4`); the WebAssembly smoke test; the demo
driven in Chrome (`tests/browser_test.mjs`); and the music report
(`tests/music_report.py`), which compares this build's pieces with release 1.8.0's.

Shared causes, in short:

- **S1 Text re-parsed in the browser.** The demo read and edited settings with
  regexes over the text, while the engine applies presets first and the last
  duplicate; JSON was refused. Fixed by one edit function and the engine's own
  resolved settings (`--resolve`, `proof.json`).
- **S2 No request identity.** Worker replies, example fetches, iframe messages and
  sheet renders had no run id, so stale results landed. Fixed with run ids,
  cancellation and tickets.
- **S3 Expression and instruments only in the browser.** Climax, dynamics, timing,
  instrument tables and ranges lived in the page, so MIDI, WAV and notation disagreed
  with it. Fixed by `parts.c` (the one instrument table) and `perform.c` (the one
  performance), exported to every output.
- **S4 Local costs only.** Every soft cost looked at one interval or one beat, or
  echoed each bar's previous bar the same way everywhere; nothing defined a subject,
  phrases, contrast, breathing or harmonic arrival, and the optimizer swept from the
  opening. Fixed by the form plan, canon-aware returns, adaptive optimization and two
  form rules.
- **S5 Direct writes.** Each writer truncated its file first. Fixed by writing beside
  the name and moving into place.

## A. Configuration and state

| # | Issue and evidence | Kind | P | Cause | Fix and check | Status |
| --- | --- | --- | --- | --- | --- | --- |
| A1 | Shape controls showed the literal text: `preset longing` (tempo 66) showed 120; JSON showed defaults | bug | P2 | S1 | controls read the resolved settings; browser test "the tempo control shows the preset's tempo 66" | fixed |
| A2 | Edits rewrote the first matching line while the engine applies the last | bug | P2 | S1 | `setKeys` updates the last line, keeps its comment, removes earlier ones; browser test on `tempo 60 … tempo 90 # brisk` | fixed |
| A3 | Ensemble choice compared with literal text; ignored with duplicates | bug | P2 | S1 | the ensemble menu is gone; per-voice `parts` set through the same edit function; browser test (clarinet) | fixed |
| A4 | A what-if from a stale score page wrote its melody into another piece's settings | bug | P2 | S2 | messages carry the run id; a stale page cannot edit; browser test "the old score is marked as the last piece that worked" | fixed |
| A5 | Example fetch raced later choices | bug | P2 | S2 | load ticket | fixed (not exercised by a test) |
| A6 | `piece.txt` broken for JSON and for text without a final newline | bug | P1 | S1 | variation merged with `setKeys`; JSON saves as `piece.json`; browser test | fixed |
| A7 | A failed run left the previous piece's downloads and share link live | bug | P2 | S2 | last good piece kept and marked; no downloads for failed settings; browser test | fixed |
| A8 | What-if ignored a `lock` on the same note | bug | P2 | S1 | the edit releases that lock | fixed (not exercised by a test) |
| A9 | Undo restored unsubmitted inputs; no redo | bug | P2 | S2 | history of composed pieces, undo and redo without composing; browser test | fixed |
| A10 | A stale what-if label leaked into later runs | bug | P2 | S2 | the label travels with its request | fixed |
| A11 | JSON configs refused edits while the controls moved | bug | P2 | S1 | JSON edited as an object; browser test | fixed |
| A12 | Re-selecting "A shared piece" loaded the first preset | bug | P2 | | the shared text is kept | fixed |
| A13 | No cancellation; every curve keypress queued a full solve | bug | P2 | S2 | a new request terminates the worker; keys debounce; browser test "of two quick composes, the later one is shown" | fixed |
| A14 | A worker that failed to load left the UI stuck | bug | P2 | | the worker is recreated on the next request | fixed |
| A15 | Outputs overwritten in place; a failed run deleted the last good piece | bug | P1 | S5 | write and move; a failed run keeps and names the earlier files; CLI test (byte-identical `canon.mid`, no `.part` files) | fixed |
| A16 | Presets stacked keys from an earlier preset | bug | P2 | | a preset starts from the defaults; unit test | fixed |
| A17 | Browser arch differed from the engine's | bug | P3 | S1 | the engine's 12-point arch; points shown from the resolved `tension` | fixed |
| A18 | Comments and multi-line matches misread | bug | P2 | S1 | the engine's comment rule; resolved settings | fixed |
| A19 | Instrument menu disagreed with the config | bug | P2 | S3 | per-voice parts from the engine | fixed |
| A20 | The optimizer's time limit makes a link machine-dependent | hyp | P2 | | the run says when a limit cut it ("limited" in `proof.json`), and the demo says so if notes change on a performance edit | mitigated |
| A21 | Sheet music could re-render an old piece | bug | P2 | S2 | render ticket | fixed |
| A22 | Iframe messages unvalidated | bug | P2 | S2 | type, run id and value checked; only player keys may be set | fixed |
| A23 | Worker errors without an id accepted | bug | P2 | S2 | every reply has its id | fixed |
| A24 | No `hashchange` | bug | P4 | | handled | fixed |
| A25 | JSON values silently truncated | bug | P2 | | refused when they do not fit | fixed |
| A26 | Text and JSON parsed differently (long comment lines, untrimmed strings, `true` for any key) | bug | P4 | | comments do not count to the line limit; strings trimmed; booleans only for on/off keys | fixed |
| A27 | `--set preset=` clobbered file keys; repeated options silently replaced | bug | P4 | | refused; CLI tests | fixed |
| A28 | Variation over 2^31 gave a misleading error | bug | P4 | | clamped to 0..99999 | fixed |
| A29 | "A surprise" chosen while busy did nothing | bug | P4 | | nothing is disabled while composing; a new compose cancels | fixed |
| A30 | Results scraped from console text | bug | P2 | S1 | results from `proof.json`; only errors from stderr | fixed |
| A31 | Browser decides JSON by content, CLI by extension | limit | P4 | | the worker names the file by content, so both agree | won't fix: consistent as is |
| A32 | "Variation 0 is the cheapest piece" is false when settings sample | bug | P4 | | wording | fixed |

## B. Playback, players and exports

| # | Issue and evidence | Kind | P | Cause | Fix and check | Status |
| --- | --- | --- | --- | --- | --- | --- |
| B1 | smplr 1.1.0 might be called wrongly, so every "sampled" voice would be the pluck synth | hyp | P1 | | refuted: the API matches smplr's README, and the browser test plays `sampled,sampled,sampled` | refuted |
| B2 | Looping scheduled from animation frames, which stop in hidden tabs; catch-up played notes late | bug | P2 | | a timer schedules 1.5 s ahead; late notes are skipped, never burst; browser test loops past one pass | fixed |
| B3 | Playback derived its own climax | bug | P3 | S3 | the form's climax drives the performance; unit test | fixed |
| B4 | Default instrument differed by output (page chamber, MIDI piano, WAV pluck) | bug | P2 | S3 | parts from the engine; WAV and the page's fallback pick the same synth per part | fixed |
| B5 | Changing the instrument while loading played the old one | bug | P2 | S3 | instruments are per voice in the config; a change composes again | fixed |
| B6 | Partial or stale files after a failure | bug | P1 | S5 | see A15 | fixed |
| B7 | A failed sample load was cached; one failure downgraded every voice | bug | P2 | | failures are not kept; each voice falls back alone and the page names it; browser test offline | fixed |
| B8 | Single-instrument ensembles load one sampler per voice | limit | P4 | | each voice needs its own sampler for its own volume and pan; only the pitches used are loaded | won't fix |
| B9 | Synth buffers built after the start time, so notes started late | bug | P2 | | built just ahead of each note | fixed |
| B10 | The mix could clip | hyp | P2 | | a hard limiter (ratio 20, knee 0, -3 dB) after the mix | mitigated; not measured |
| B11 | Instrument ranges only counted in `report.txt`; dance and lament went out of range | bug | P1 | S3 | ranges are rules per voice; impossible combinations explained; unit, CLI and browser tests | fixed |
| B12 | A browser-only bass line outside the rules and the files | limit | P3 | S3 | a labelled playback option, off by default, on the engine's chord roots | fixed |
| B13 | ABC players sounded transposing parts at written pitch | bug | P3 | | `%%MIDI program` and `%%MIDI transpose`; unit test | fixed |
| B14 | MIDI and WAV had no dynamics, tempo shaping or articulation | bug | P2 | S3 | one performance; MIDI tempo map, velocities, lengths, volume and pan; WAV the same; unit tests | fixed |
| B15 | voices.wav used one synth for every voice | bug | P3 | S3 | `instrument auto` per part family; documented as synths | fixed |
| B16 | Toggling Loop late broke the ending | bug | P4 | | changing Loop, Sound or Bass restarts | fixed |
| B17 | Parts could swap after any recompose | bug | P2 | S3 | assignment from transpositions, never notes; unit test | fixed |
| B18 | Synth nodes accumulated over long loops | bug | P4 | | released when they end | fixed |
| B19 | Playhead jumped during the final hold | bug | P4 | | held on the last step | fixed |
| B20 | Stop clicked | bug | P4 | | 12 ms fade | fixed |
| B21 | Each loop pass repeats the dynamic arc | limit | P4 | | by design: each pass of a round is the whole piece | won't fix |
| B22 | Highlighting follows written, not sounding, lengths | limit | P4 | | | won't fix |
| B24 | The page's synths are similar to, not the same as, voices.wav's | limit | P4 | | same choice per part, same Karplus-Strong; sample rates differ | documented |
| B26 | MusicXML had no tempo; LilyPond no MIDI instrument | bug | P3 | | `<metronome>`/`<sound tempo>`; `midiInstrument` | fixed |
| B27 | Low saxes at concert pitch stayed in treble clef | bug | P4 | | bass clef at concert pitch | fixed |
| B30 | Instrument names differed between page and engine | bug | P4 | S3 | names from the engine | fixed |
| B33 | Write failures named no file | bug | P4 | S5 | each failed file is named | fixed |
| B35 | A very long, slow piece needs ~270 MB for its WAV | limit | P4 | | bounded by SPAN_MAX at tempo 20 | won't fix |
| B36 | While a piece played, the button kept saying "Loading 100%" (a progress frame queued before loading ended covered "Stop"), found by the browser test's state log | bug | P2 | | the progress callback stops once loading has | fixed |

## C. Solver, parsers and native code

No memory-safety bug was found, and no path reports a search limit as "unsat".

| # | Issue and evidence | Kind | P | Fix and check | Status |
| --- | --- | --- | --- | --- | --- |
| C1 | "Converged" printed when a limit cut the last window | bug | P1 | a limit stop sets `limited`; the summary says so | fixed |
| C2 | Each solve had its own clock; `--sat` ignored `time_limit` | bug | P2 | one deadline per command, SAT included | fixed |
| C3 | Time checked every 64 nodes | hyp | P4 | every 8 | fixed |
| C4 | "killed: the given notes alone" when the core search hit a limit | bug | P1 | "killed: unknown" without a core; "(approximate)" when one is | fixed |
| C5 | A locked note outside the range blamed `lock` alone | bug | P2 | explained before the search; CLI test | fixed |
| C6 | Optimizer overshot its budget by a window | bug | P4 | the last window ends at the budget | fixed |
| C7 | A refutation listed every decision in force as its reason, unlabelled | bug | P2 | labelled "(not minimized)" | fixed |
| C8 | A value forced by refutation was "forced by propagation" | bug | P2 | "forced by the search"; `bySearch` in JSON | fixed |
| C9 | Look-ahead ordering inflated the statistics | bug | P4 | restored after each look-ahead | fixed |
| C10 | A trial that could not be built counted as unsat | bug | P2 | counted as a limit | fixed |
| C11 | JSON strings truncated | bug | P2 | see A25 | fixed |
| C12 | Empty list items dropped (`60,,62`) | bug | P4 | refused | fixed |
| C13 | JSON numbers that underflow accepted (`1e-400` gave 0 = no limit) | bug | P4 | refused | fixed |
| C14 | A JSON array item holding several values split silently | bug | P4 | refused | fixed |
| C15 | Config files read without a size limit | bug | P4 | 1 MB | fixed |
| C16 | MIDI reader quadratic on overlapping same-pitch notes | bug | P4 | | won't fix: inputs are capped at 16 MB |
| C17 | `--melody-midi` chose a track of pitch-0 notes | bug | P4 | the first track with a sounding note | fixed |
| C18 | Node counters are 32-bit on Windows | hyp | P4 | | won't fix: needs over 2^31 nodes with every limit off |
| C19 | `clock()` is CPU time on POSIX | bug | P4 | wall clock (`timespec_get`) | fixed |
| C20 | A delay search was rejected when its longest delay was too long | bug | P4 | judged by the shortest | fixed |
| C21 | Proof parents capped at 8 | limit | P4 | | won't fix |
| C22 | Lone UTF-16 surrogates accepted | bug | P4 | refused | fixed |
| C24 | `--count` counts assignments (ties, chords), not melodies | limit | P3 | the demo says "assignments of notes, ties and chords" | documented |
| C26 | `max_nodes` thought off by one | hyp | P4 | | not a bug: the node that stops is counted |
| C27 | A rejected list value cleared the whole list (`parts=kazoo` emptied `parts`), found by a new test | bug | P2 | the list changes only when all of it is valid | fixed |

## D. Music

| # | Issue and evidence | Kind | P | Cause | Fix and check | Status |
| --- | --- | --- | --- | --- | --- | --- |
| D1 | No subject, development, contrast or breathing: every cost is local, and `w_sequence` echoes every bar the same way | limit | P3 | S4 | the form plan (`phrase`): subject, returns, contrast, breaths, arrivals; unit tests | fixed |
| D2 | A return of the subject sounds against the next voice's entry of it, so it would move in parallel (forbidden at the octave) | bug | P3 | S4 | `head_collides`: such a return is inverted, contrary to the entry; unit test | fixed |
| D3 | The optimizer swept windows from the opening and never reached far returns | bug | P3 | S4 | the costliest window first; zero-cost windows skipped; unit test | fixed |
| D4 | Global promises broken: in the report the subject's head broke in 7 to 10 of 15 steps even at weight 10, and longing rose far above its climax (`arc 594`) | bug | P3 | S4 | the subject's rhythm and the climax as rules (`form`); unit tests; music report | fixed; measured |
| D5 | Rests and holds cost everywhere, so phrases never breathed | limit | P3 | S4 | free in the last half bar of each phrase, and a breath cost | fixed |
| D6 | Harmony had no direction before the final cadence | limit | P3 | S4 | arrival terms: V at phrase ends, prepared by IV or ii | fixed |
| D7 | Longing modulated to C minor, not the C major it promised | bug | P3 | | `mode_second=major` | fixed |
| D8 | Expression differed between page and files | bug | P3 | S3 | see B14 | fixed |
| D9 | Whether the pieces now move a listener | hyp | P3 | | the music report's numbers and its before/after recordings; needs listening | unverified |

## E. Interface

| # | Issue | Kind | P | Fix and check | Status |
| --- | --- | --- | --- | --- | --- |
| E1 | Raw config, statistics and proof dominated the page | limit | P2 | Start, Shape, score and Players, Save; the text, counting and output under Advanced; the solver's workings under "Under the hood" | fixed |
| E2 | Controls without names | bug | P2 | browser test: every visible control has an accessible name | fixed |
| E3 | Phone layout | limit | P2 | browser test: no sideways scroll at 390 px | fixed |
| E4 | Keyboard | limit | P2 | curve points by arrow keys (composing once they rest), Space plays, focus rings | fixed |
