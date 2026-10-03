# Canon Collapse: design notes

These notes cover the ideas behind Canon Collapse and how far the code has taken
each one. Each section ends with a status line:

- **Built** – in the code, tested, and documented in [config.md](config.md).
- **Partly built** – a simpler form exists; the note says what is missing.
- **Not built** – an idea only.

(The project's working title was "Gödel's Fugue"; the name is now Canon Collapse.)

## 1. The idea

A composition starts as a space of unresolved possibilities. Every note, tie, chord
and key is a variable with a set of values still allowed. Musical rules are
constraints: they remove values that cannot be part of any valid piece. Choosing
one value shrinks other sets, and those removals spread through the piece until
every variable holds exactly one value.

The engine records why each value was removed, so every note in the result can be
explained. A note is either **chosen** (the search picked it among several legal
values, and the explanation lists the alternatives and their costs) or **forced**
(every other value broke a rule, and the explanation names each rule and the
decisions it rests on).

"Collapse" here is a mathematical analogy for constraint propagation. The notes are
not quantum states and nothing simulates wave-function collapse.

## 2. Why a canon

A canon plays one melody against delayed, possibly transformed, copies of itself.
With melody $M = (x_0, \dots, x_{N-1})$, voice $j$ sounds

$$V_j(t) = T_j\big(M(t - d_j)\big)$$

where $d_j$ is the voice's entry delay and $T_j$ its transform. Every melody note is
heard in several harmonic contexts. With delay 4 in two voices, $x_0$ sounds against
$x_4$ at step 4, so choosing $x_0$ constrains $x_4$, which sounds against $x_8$ at
step 8, and so on. Decisions have consequences far away in musical time; this is the
nonlocal propagation that makes the problem interesting.

**Built** in `canon.c`: `canon_map_source` gives the melody index each voice plays at
each step, and every rule is built from it.

## 3. The model

The config becomes a model (`model.c`) of variables, domains and constraints.

- **Variables**: one pitch per melody note; with `rhythm`, a tie per note (a new
  attack or a continuation of the previous pitch) and a rest value in the pitch
  domain; with `harmony`, a chord per bar (scale degrees I–vii); and one key per
  section, two when the piece modulates.
- **Domains**: every kind of variable stores its values in one 128-bit set
  (`MidiDomain`), so pitch, tie, chord and key share the same operations.
- **Hard constraints** remove values. Each covers the few variables it involves,
  and a slot names the voice each pitch is heard in, so transforms are applied
  where the rule looks at the pitch.
- **Soft terms** add cost without removing anything. Their sum is the piece's
  **energy**, and the search tries cheaper values first.

The state space for $N$ notes with 8 legal pitches each is $8^N$ before any rule
applies; the engine never enumerates it.

**Built.**

## 4. Hard rules

| Rule | What it forbids | Status |
| --- | --- | --- |
| scale | a sounding pitch outside the key in force at that step | Built |
| range | any voice sounding outside `range_low`..`range_high` | Built |
| melodic leap | consecutive notes of any voice more than `max_leap` apart | Built |
| leading tone | with `leading_tone 1`, a melody note a semitone below the tonic of the key in force followed by anything but that tonic; a tie passes the duty to the next attack, a rest does not resolve it, the last note is free | Built |
| double leap | with `double_leaps 0`, two leaps over 4 semitones in a row in one direction between three consecutive notes of any voice; a repeated or held note or a silence between them separates them | Built |
| consonance | on strong beats (or every step), any pair of sounding voices other than octave, third, fifth or sixth; a fourth only between upper voices; unisons opt-in | Built |
| parallel fifths / octaves | two voices moving in the same direction from a perfect interval to the same one | Built |
| spacing | two voices sounding more than `max_spacing` semitones apart | Built |
| crossing | with `crossing 0`, a later voice sounding above an earlier one | Built |
| chord tone | a strong-beat note outside the bar's chord | Built |
| progression | consecutive bar chords outside the usual root progressions | Built |
| cadence | a melody that does not end on the tonic approached from the dominant triad; a follower not ending on a tonic-triad note; with harmony, a last bar other than I or a bar before it other than V or vii | Built |
| tie / max hold / rests | a tie that changes pitch or holds a rest, a note held too long, more than `max_rests` rests | Built |
| modulation | a searched second key that is not closely related to the first | Built |
| lock | a locked note, or a note given by `melody`, taking any other pitch | Built |
| mirror | a melody that is not its own retrograde inversion around `mirror_axis` $a$: $x_i + x_{N-1-i} = 2a$, a rest pairs only with a rest, and an odd length's middle note is $a$ | Built |

Rules from the design that are **not built**: suspensions and their resolution,
chordal sevenths resolving, a registral-centre invariant, and controlled symmetry
breaking (a mirror melody with a few pairs allowed off the axis). Direct (hidden)
fifths and octaves are a soft cost rather than a rule, and so is the motion balance
between voices (`w_contrary`).

## 5. Energy

$$E(s) = \sum_k w_k \, c_k(s)$$

Each term $c_k$ looks at a few variables and belongs to one category, so the energy
can be broken down by rule (`report.txt`, `score.html`):

- **gravity** – tonal potential of each scale degree (tonic and mediant stable,
  leading tone and chromatic notes unstable), relative to the key in force;
- **curve** – distance from a tension target over the melody: a built-in arch, or the
  curve drawn with `tension`, whose points spread evenly over the melody and are
  joined by straight lines;
- **leap**, **repeat**, **recovery** – interval size, striking a pitch twice, and
  failing to step back after a leap larger than a third;
- **motif** – breaking a repeating interval pattern;
- **dissonance** and **direct perfect** – graded vertical dissonance, and similar
  motion into a fifth or octave;
- **contrary motion** – each pair of voices both moving up or both moving down from
  one step to the next (`w_contrary`), the motion balance between voices;
- **rest**, **hold**, **syncopation**, **rhythm**, **final** – rhythm preferences;
- **chord**, **chord motion**, **non-chord tone** – harmony preferences;
- **key**, **key distance** – accidentals of a searched key, and the distance between
  the two keys of a modulation;
- **corpus** – per-pitch-class costs learned from `--corpus`, scaled to `w_corpus`.

The cost the search gives a value is the sum of the terms it completes. When every
other variable is fixed, that is exactly the change the value makes to the total
energy; a test checks this across every canon shape.

**Temperature.** With `temperature` $T > 0$, values are sampled with probability
$P \propto e^{-c/T}$ instead of taken cheapest first. `anneal_*` cools $T$ linearly
or geometrically over the decisions; a schedule whose end is above its start heats
instead, which is the "reverse annealing" idea.

**Optimization.** The search keeps the first solution it reaches. With `optimize`,
the engine then runs large neighbourhood search. A window of six notes slides along
the melody, everything outside it is fixed to the best piece so far, and branch and
bound looks for a strictly cheaper piece inside the window. The best piece is replayed
from the root so the proof describes it.

**Built.** **Not built**: emergent motif detection and "motif pressure" that
reinforces discovered motifs, rule weights that change with musical form, and
weights learned from example pieces.

## 6. Information and entropy

With $|D_i|$ values left for variable $i$, the unresolved information is

$$H = \sum_i \log_2 |D_i|$$

It is logged after every round of propagation (`entropy.txt`, the chart in
`score.html`) and reaches 0 when the piece is complete.

The next variable to decide is chosen by `var_order`:

- **mrv** – fewest remaining values, ties to the variable in the most constraints
  (the design's "maximum influence");
- **entropy** – lowest Shannon entropy of the Boltzmann distribution over its
  values' costs: the variable whose choice is most certain;
- **collapse** – for the few most constrained variables, try each value, propagate,
  and pick the variable with the lowest expected remaining entropy (the design's
  "expected collapse");
- **index** – melody order.

With `hierarchy` the search decides keys first, then chords, then notes and ties.

**Built.**

## 7. Canon transforms

Transposition, inversion around a MIDI axis ($T(p) = 2a - p$), pitch-class inversion
that keeps each note's octave, retrograde, augmentation, diminution, per-voice
entry delays, a phase offset, and cyclic canons are **built**. The program warns when
an inversion axis maps scale notes out of the key and suggests one that keeps them
all.

Each follower may take its own transposition (`transpose_1` to `transpose_3`; the
default `same` uses `transpose`), so voice 2 can answer at the fifth and voice 3 at
the octave. Diatonic transposition (`diatonic`) is **built** too: transpositions then
count steps of the key's seven-note scale (natural minor for minor), so a canon at
the third stays in the key and its thirds come out major or minor as the key wants.
A note outside that scale (minor's raised 7th, or any chromatic note) moves with the
scale note just below it and keeps its distance above it, so $G\sharp$ in A minor
moves a step up to $A\sharp$. Diatonic steps need one fixed key: key search, mode
search and modulation are rejected. Inversion comes first, then transposition. A
chromatic transposition keeps interval sizes, so one leap check of a pair of melody
notes covers the leader and every follower; a diatonic one does not (C-E is a major
third, E-G a minor one), so the leap rule is built once for each distinct line shape.

In a cyclic canon with delay $d$ and length $N$, following one note to its copy
$d$ steps later partitions the positions into $\gcd(N, d)$ cycles, each of length
$N / \gcd(N, d)$; the rules couple positions along those cycles.

**Not built**: fractional delays on a finer tick grid, other transforms (inversion,
retrograde, augmentation) chosen per voice, and composing transforms as group
elements (the dihedral group of transpositions and inversions).

## 8. Keys, modes and modulation

Keys are variables whose values are (tonic, mode) pairs: major, minor (natural minor
with the raised leading tone), dorian, phrygian, lydian, mixolydian and locrian.
With `key search` or `mode search` the solver chooses. A lock on F#, for example,
removes every key without F#, and cost then picks among the rest. With
`modulate_at` every voice changes key at that step, so a melody note heard on both
sides of the change must suit both keys. The second key can be named or searched
among the closely related keys (key signatures at most one accidental apart), and
its distance costs `w_modulate` per step on the circle of fifths.

**Built.** **Not built**: tonal probability that weighs keys instead of removing
them, modulation through more than one step of the key graph, and naming the pivot
chord in explanations (the chord at a key change may be anything).

## 9. Hierarchical collapse: harmony and rhythm

With `harmony`, each bar has a chord variable. Strong-beat notes must be its chord
tones, and consecutive chords follow the usual root progressions (I anywhere; ii to
V or vii; IV to V, vii, I or ii; V to I or vi; vi to ii, IV, V or iii; vii to I).
Repeating a chord is always allowed but costs extra. Because each melody note is heard
in later bars too, a canon at a one-bar delay tends to keep harmonies that share
tones, as rounds do.

With `rhythm`, each note has a tie variable and the pitch domain gains a rest, so
durations from a quarter to a whole note appear on the one-step grid. MIDI, MusicXML,
LilyPond, ABC and WAV carry the real durations.

**Built.** **Not built**: cadence type as a variable (authentic, half, deceptive),
phrase boundaries as variables, meter as a variable, per-voice meters (`poly_meter`
changes which steps are strong for every voice), and form as a state machine.

## 10. Search

- **Propagation**: generalized arc consistency over each constraint's scope, with the
  last support found cached per value; a counting propagator for the rest limit; a
  queue of constraints to revisit.
- **Backtracking** with snapshots on the heap (one per depth). A trail would use less
  memory; at up to about 160 variables (a 64-note melody with ties, a chord per
  bar and two keys), snapshots are simple and fast enough.
- **Conflict-directed backjumping**: every variable carries the set of decisions its
  domain depends on. A dead end jumps straight back to the latest decision in that
  set, and the failed value is removed with that set as its reason.
- **Learned conflicts** (nogoods): each failed combination of decisions is stored and
  propagated like a constraint.
- **Limits**: `max_nodes` and `time_limit` bound the search, the unsat core and the
  optimizer; hitting one exits with status 3.
- **Delay as a variable**: `delay_search` treats the delay as the outermost variable,
  solving every delay in a range and keeping the cheapest.
- **SAT backend**: `--sat` encodes each constraint as clauses forbidding the tuples it
  rejects and runs DPLL, as an independent check on the same rules.

**Built.** **Not built**: an SMT encoding.

## 11. Explanations

- **Proof trace** (`proof.txt`): every removal, decision and forced collapse in order.
- **Proof DAG** (`proof.dag`, `proof.json`): each removal lists the events and
  assignments it rests on.
- **Why is this note here?** (`--explain`, `explain.txt`, `score.html`): the
  variable's status, the rule that removed each other value, and the decisions a
  forced value rests on. For a chosen value, the ranked candidates and each soft
  rule's share of their costs.
- **Minimal explanations**: the decisions behind a forced value's removals are
  an over-approximation, since a decision's own reason includes whatever shaped
  its domain before it was made. They are replayed from the root with
  propagation alone and cut by deletion, latest first: a decision goes when the
  rest still force the value. Propagation only removes more values as decisions
  are added, so the set left is minimal: it forces the value, and without any one
  of its decisions it does not. When all of them together do not force the value
  (the search's refutations or learned conflicts did part of the work), the full
  set is shown, marked as not minimized (`"minimal": false` in JSON). The cost is
  one replay per decision in the reason, plus one of the whole reason. That one
  fixes its decisions in turn from a root fixpoint built once per pass over the
  variables and keeps the fixpoint before each, so the replay without a decision
  starts from the fixpoint of the decisions below it, which are all still in the
  set. A pass spends at most 1000 replays, and forced values past that keep their
  full set, marked the same way.
- **Unsat core**: deletion over the rules in use. A rule stays in the core only if
  the problem becomes solvable without it.
- **Counterfactual**: with a lock or given notes, the same rules are solved without
  them and the notes that change are listed.
- **Counting completions** (`--count N`): the search counts each piece and fails on
  every decision level, so learned conflicts exclude only counted pieces. The count is
  exact unless N, `max_nodes` or `time_limit` stopped it, and then a lower bound.
- **Sensitivity and bifurcation points** (`--sensitivity`): each melody note is fixed
  to each value left by root propagation and solved. One viable value makes the note
  frozen, several make it a bifurcation point.
- **Violations** (`--check`, a failed run's stderr, `report.txt`, `proof.json`):
  every rule instance on a variable fixed before the search (given notes, locks,
  `rest_at`, a fixed key) is broken when no values of its free variables, each from its
  initial domain (a tie between two different given notes can only start a new note),
  satisfy it; each one broken is named with its fixed notes, e.g. `consonance: voices
  1,2 must be consonant at step 4 (bar 2) (x4 = D4, x0 = C4)`, so nothing is reported
  that a completion could still satisfy. Past 64 the rest are counted ("... and N
  more", `violationsMore` in `proof.json`).

**Built**, with forced values explained minimally under propagation. **Partly
built**: contradiction certificates are the unsat core plus the last removal, naming
broken rule instances only for given notes. **Not built**: measuring how much the
rest of the piece changes when a note is perturbed (sensitivity lists only the viable
values).

## 12. Interface

`score.html` is a self-contained page. It has a piano roll with one lane per voice,
chord symbols and the key change, Web Audio playback, an inspector that explains any
note and links to the variables it depends on, the entropy curve, energy and
rule-impact charts, and a filterable proof log with links to each event's parents.

The Collapse card replays the proof: a slider and play, pause and step buttons move
through the events, and a grid shows every melody note's remaining candidate pitches
(rests in their own row) at the chosen event, with the value removed by that event
marked and the entropy chart's cursor at the same place. Each variable's JSON carries
its initial domain; the domain at event k is that minus every value removed by events
0..k, and a decision or forced event leaves only its value. The proof log holds exactly
the final search path, so the replay is exact, and it works for unsolved runs too.
When the config gives notes, a panel shows the piece solved without them and which
notes changed.

The web demo (`web/`, published with GitHub Pages) runs the same program compiled to
WebAssembly in a web worker: it edits a config, composes, and shows this page with
every output file to download.

**Built.** **Not built**: a constraint-graph view, editing a single note in the score
and re-solving, a counterfactual inspector that tries other values, and weight sliders.

## 13. Presets and modes

`renaissance`, `baroque`, `classical`, `minimalist` and `experimental` bundle key,
mode, rules and weights. They are rule systems, not imitations of historical style.

**Built.** **Not built**: changing the rules part-way through a piece ("rule
mutation"), and separate physics, counterpoint and research modes. The outputs
already expose the research data: entropy, statistics, and removals by rule.

## 14. Implementation

- C11, no dependencies, builds warning-free with GCC, Clang and MSVC, and with
  Emscripten for the browser.
- Config: one table of keys drives text and JSON configs, `--set`, validation,
  presets and [config.md](config.md). `report.txt` records every key used, so a piece
  can be reproduced from it.
- Inputs: text and JSON configs, and Standard MIDI files of format 0, 1 or 2
  (`midi_read.c`). `--melody-midi` reads a melody from the first track with notes,
  rounding onsets and ends to the nearest quarter note: a held note gives its pitch
  on every step it covers, the highest of overlapping notes wins, and a gap is a rest.
  `--corpus` counts the pitch classes of their notes as it does for lists of pitches;
  notes on channel 10, the General MIDI drums, are skipped.
- Outputs: MIDI, MusicXML, LilyPond, ABC, WAV, SVG, text and JSON traces, and the
  HTML page. The notation exports share one spelling (`key_spell`), cut notes at
  barlines and at the key change with ties, and pad the last bar with a rest.

## 15. Research directions

Most of these are not built; the proof log and statistics provide the data for them.

- How do delay and voice count affect the number of solutions, the propagation
  graph, and how fast entropy falls? **Partly built**: `--count` counts the solutions
  of one config.
- Which rules remove the most values (the rule-impact table is a start)?
- Which melody positions are most structurally sensitive? **Built**: `--sensitivity`
  lists each note's viable values and marks it frozen or a bifurcation point.
- How does temperature trade novelty against cost over many samples?
- Analysis mode: measure an existing score against the rules, estimate weights, and
  find recurring structures ("style fingerprints"). **Partly built**: `melody`, or
  `--melody-midi` for a melody in a MIDI file, checks a given melody against the rules
  and reports its energy or the rules it breaks.
