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

Rules from the design that are **not built**: suspensions and their resolution,
leading tones and chordal sevenths resolving, limits on repeated large leaps, a registral-centre invariant, motion balance between
voices, and symmetry constraints ($x_i + x_{N-1-i} = 2a$) with controlled symmetry
breaking. Direct (hidden) fifths and octaves are a soft cost rather than a rule.

## 5. Energy

$$E(s) = \sum_k w_k \, c_k(s)$$

Each term $c_k$ looks at a few variables and belongs to one category, so the energy
can be broken down by rule (`report.txt`, `score.html`):

- **gravity** – tonal potential of each scale degree (tonic and mediant stable,
  leading tone and chromatic notes unstable), relative to the key in force;
- **curve** – distance from an arch-shaped tension target over the melody;
- **leap**, **repeat**, **recovery** – interval size, striking a pitch twice, and
  failing to step back after a leap larger than a third;
- **motif** – breaking a repeating interval pattern;
- **dissonance** and **direct perfect** – graded vertical dissonance, and similar
  motion into a fifth or octave;
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

**Built.** **Not built**: tension curves the user draws, emergent motif detection and
"motif pressure" that reinforces discovered motifs, rule weights that change with
musical form, and weights learned from example pieces.

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

In a cyclic canon with delay $d$ and length $N$, following one note to its copy
$d$ steps later partitions the positions into $\gcd(N, d)$ cycles, each of length
$N / \gcd(N, d)$; the rules couple positions along those cycles.

**Not built**: fractional delays on a finer tick grid, diatonic (in-key) transposition,
different transforms per voice, and composing transforms as group elements (the
dihedral group of transpositions and inversions).

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
durations from a quarter to a whole note appear on the one-step grid. MIDI, MusicXML
and WAV carry the real durations.

**Built.** **Not built**: cadence type as a variable (authentic, half, deceptive),
phrase boundaries as variables, meter as a variable, per-voice meters (`poly_meter`
changes which steps are strong for every voice), and form as a state machine.

## 10. Search

- **Propagation**: generalized arc consistency over each constraint's scope, with the
  last support found cached per value; a counting propagator for the rest limit; a
  queue of constraints to revisit.
- **Backtracking** with snapshots on the heap (one per depth). A trail would use less
  memory; at up to about a hundred variables, snapshots are simple and fast enough.
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
- **Unsat core**: deletion over the rules in use. A rule stays in the core only if
  the problem becomes solvable without it.
- **Counterfactual**: with a lock or given notes, the same rules are solved without
  them and the notes that change are listed.

**Built.** **Partly built**: the decisions a forced value "depends on" are an
over-approximation, not a minimal explanation, and contradiction certificates are the
unsat core plus the last removal. **Not built**: structural sensitivity (perturb each
note and measure the change), bifurcation points, and counting completions.

## 12. Interface

`score.html` is a self-contained page. It has a piano roll with one lane per voice,
chord symbols and the key change, Web Audio playback, an inspector that explains any
note and links to the variables it depends on, the entropy curve, energy and
rule-impact charts, and a filterable proof log with links to each event's parents.

**Built.** **Not built**: animating the collapse step by step, a constraint-graph
view, editing a note in the browser and re-solving, a counterfactual inspector, and
weight sliders.

## 13. Presets and modes

`renaissance`, `baroque`, `classical`, `minimalist` and `experimental` bundle key,
mode, rules and weights. They are rule systems, not imitations of historical style.

**Built.** **Not built**: changing the rules part-way through a piece ("rule
mutation"), and separate physics, counterpoint and research modes. The outputs
already expose the research data: entropy, statistics, and removals by rule.

## 14. Implementation

- C11, no dependencies, builds warning-free with GCC, Clang and MSVC.
- Config: one table of keys drives text and JSON configs, `--set`, validation,
  presets and [config.md](config.md). `report.txt` records every key used, so a piece
  can be reproduced from it.
- Outputs: MIDI, MusicXML, WAV, SVG, text and JSON traces, and the HTML page.

## 15. Research directions

None of these are built; the proof log and statistics provide the data for them.

- How do delay and voice count affect the number of solutions, the propagation
  graph, and how fast entropy falls?
- Which rules remove the most values (the rule-impact table is a start)?
- Which melody positions are most structurally sensitive?
- How does temperature trade novelty against cost over many samples?
- Analysis mode: measure an existing score against the rules, estimate weights, and
  find recurring structures ("style fingerprints"). **Partly built**: `melody` checks a
  given melody against the rules and reports its energy or the rules it breaks.
