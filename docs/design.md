# Gödel's Fugue: design notes

These notes cover the ideas behind Gödel's Fugue and how far the code has taken
each one. Each section ends with a status line:

- **Built** – in the code, tested, and documented in [config.md](config.md).
- **Partly built** – a simpler form exists; the note says what is missing.
- **Not built** – an idea only.

(The name nods to Hofstadter's *Gödel, Escher, Bach*, whose dialogues are written as
canons and fugues.)

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
- **step** – each interval past a whole step, by its size (`w_step`): a third costs
  1, a fourth 2, a fifth 3, an octave 5. **leap** charges whole multiples of four
  semitones, so it leaves thirds free and a line can hop between chord tones; this
  favours stepwise lines;
- **motif** – breaking a repeating interval pattern;
- **arc** – the melody's shape around one climax (`w_arc`). The climax is the step
  `climax` percent of the way through, moved to the nearest strong beat; every other
  note pays for each semitone it rises above the climax note (this times
  1 + semitones / 3), and a note in the first quarter also pays unless it is at least
  a major third below. The melody has one peak, reached once and left behind, and
  begins low enough that the climb is heard. The cost can only be paid where the range
  leaves room: a peak at the top of the range is free, so a preset that wants an
  early peak widens the range below it;
- **subject**, **contrast**, **breath**, **arrival** – the form's terms, below;
- **sequence** – without a form, a bar that does not echo the bar before (`w_sequence`): for each
  step, the direction of the interval into it is compared with the one a bar earlier.
  Opposite directions cost the weight, a different interval in the same direction
  half of it, and a match or a move of a semitone or less nothing. Bars then repeat a
  figure at another pitch, the sequence that carries a line forward;
- **dissonance** and **direct perfect** – graded vertical dissonance, and similar
  motion into a fifth or octave;
- **contrary motion** – each pair of voices both moving up or both moving down from
  one step to the next (`w_contrary`), the motion balance between voices;
- **rest**, **hold**, **syncopation**, **rhythm**, **final** – rhythm preferences,
  **run** – two notes shorter than a beat in a row that leap more than a whole step,
  on the eighth and sixteenth grids (`w_run`), and **figure** – the rhythm figure of
  each beat on the sixteenth grid (`w_figure`, section 9);
- **chord**, **chord motion**, **non-chord tone** – harmony preferences;
- **key**, **key distance** – accidentals of a searched key, and the distance between
  the two keys of a modulation;
- **corpus** – per-pitch-class costs learned from `--corpus`, scaled to `w_corpus`.

The cost the search gives a value is the sum of the terms it completes. When every
other variable is fixed, that is exactly the change the value makes to the total
energy; a test checks this across every canon shape.

**Form.** A melody that only avoids mistakes is forgettable: nothing in it comes
back, nothing contrasts, and it never stops to breathe. With `phrase` set, the melody
gets a plan of phrases that many bars long (`form_plan` in `model.c`): the first
states the subject; the phrase holding the climax contrasts with it; the last returns;
the others develop. The plan is computed once from the config and shared by the
model, the performance and the score page (`form` in `proof.json`), and its terms
reuse the existing weights:

- **subject**: each development and the return bring back the subject's head, its
  first bar. Its rhythm, its rests and its contour (each move up, down or level) are
  rules (`form`), since they are what makes a theme recognizable; the sizes of the
  moves are a cost (`w_sequence`): the same size or a semitone off is free, another
  size costs half, so a return can be a sequence a step higher or a variation, and the
  counterpoint keeps room. The return also starts on the subject's own first pitch. In a canon whose
  entries fall on phrase starts, the leader's development sounds while the next voice
  states the subject; the same contour there would move in parallel against it, which
  the rules forbid at the octave. So where a head would sound against another voice's
  subject (`head_collides`), it is brought back upside down instead, the classical
  inversion, which moves in contrary motion against the entry.
- **contrast** (`w_sequence`): the climax's bar moves in clearly more or fewer notes
  than the head (attacks sampled every step, or every eighth on the sixteenth grid,
  differing by a quarter of the samples).
- **breath** (`w_rhythm`): every phrase but the last ends with a long note or a rest
  in its last half bar; rests and ties cost nothing there.
- **arrival** (`w_harmony`): with harmony, each of those phrase ends has a dominant
  chord prepared by IV or ii, and so does the final cadence, so the harmony moves
  toward its arrivals instead of choosing each bar on its own.

With the arc (`w_arc`, the climax placed by `climax`), the form makes the climax a
rule too: it sounds, nothing rises above it, and only notes within a held note's
length of it reach its pitch, so it is the melody's single top rather than a ceiling
touched again and again. As costs, both the returns and
the climax were broken in most pieces: they relate notes far apart, and the
optimizer's windows only see a few neighbouring notes at a time; measured over the
presets and several variations, raising the weights only raised the cost of breaking
them, and with the rhythm alone a rule, a repeated note or a dissonance still
outweighed the contour. Together this gives a subject that is heard again, a contrasting climax, places to breathe and a prepared ending. The
canon itself already repeats the subject at every entry; the form is what makes the
melody between the entries develop it. `phrase 0` keeps the old bar-to-bar sequence
cost, so strict canons without a form work as before.

**Temperature.** With `temperature` $T > 0$, values are sampled with probability
$P \propto e^{-c/T}$ instead of taken cheapest first. `anneal_*` cools $T$ linearly
or geometrically over the decisions; a schedule whose end is above its start heats
instead, which is the "reverse annealing" idea.

**Optimization.** The search keeps the first solution it reaches. With `optimize`,
the engine then runs large neighbourhood search. A window of six notes is freed,
everything outside it is fixed to the best piece so far, and branch and bound looks
for a strictly cheaper piece inside the window. The next window is the one whose notes
the best piece's costs fall on most, among those not tried since the last
improvement; a window no cost touches cannot improve and is skipped. A fixed sweep from
the first note spent the whole budget on the opening and never reached a far return
of the subject. The best piece is replayed from the root so the proof describes it.
"No window improves it" is said only when every window was searched; a run that
`max_nodes` or `time_limit` cut short says so, since another machine may get further.
`time_limit` holds for the whole command: the piece, the delays tried, the unsat core,
the counterfactual and the SAT backend share one deadline.

**Built.** **Partly built**: weights that follow musical form (the form's terms).
**Not built**: emergent motif detection and "motif pressure" that reinforces
discovered motifs, and weights learned from example pieces.

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

**The step grid.** A step is a quarter note; with `grid eighth` an eighth, so a beat
is two steps and a 4/4 bar eight; with `grid sixteenth` a sixteenth, four to a beat
and sixteen to a bar. Ties give the longer values: eighths, quarters, dotted quarters,
halves and longer on the eighth grid (`max_hold` reaches 7, a whole note), and
eighths, dotted eighths, quarters, dotted quarters and halves on the sixteenth grid.
Everything that thinks in bars or beats follows the grid, through one count of steps
to a beat (`config_beat_steps`):

- a step is strong only where a strong beat starts (the bar's first beat, and every
  third beat with `poly_meter`), so `consonance strong` and the chord tones still
  fall on the downbeat and an off-beat eighth or sixteenth is always weak;
- each bar has one chord;
- **syncopation** costs a note attacked on beat 2 or 4 and held over the next beat,
  as on the quarter grid, and also a note attacked off the beat and held across the
  next one: on the "and" on the eighth grid, and on the "e", the "and" or the "a" on
  the sixteenth grid;
- **rhythm** costs a bar with no rhythmic variety: eight plain eighths or four plain
  quarters, and on the sixteenth grid also sixteen plain sixteenths;
- **run** costs two notes in a row that are both shorter than a beat and leap more
  than a whole step, since fast notes move by step: two eighths on the eighth grid,
  and any pair of sixteenths, eighths and dotted eighths on the sixteenth grid. It
  looks at the tie of the second note's first step and at the ties of the steps less
  than a beat away on either side: an attack among those before makes the first note
  short, and one among those after the second.

**Rhythm figures.** A grid where every step may start a note gives frantic,
random-sounding rhythm: nothing stops a sixteenth on the "e" of one beat, an eighth
across the next and three more sixteenths after it. Real music builds each beat from a
small vocabulary of figures, and its sixteenths come in beat-sized groups that start
on the beat. So on the sixteenth grid each whole beat of the melody has a term over
the ties of its four steps (`w_figure`, default 3, the same as `w_syncopation` and
`w_rhythm`). Writing x for a step where a note or a rest starts and . for one held
from before, the cost is `w_figure` times the figure's grade:

| Grade | Figures |
| --- | --- |
| 0 | `x...` a quarter or a longer note from the beat, `x.x.` two eighths, `....` a beat held over |
| 1 | `x.xx`, `xxx.`, `xxxx` (sixteenth figures from the beat), `x..x` (a dotted eighth and a sixteenth), `..x.` (an eighth on the "and" after a held one) |
| 2 | `xx..`, the snap: a sixteenth and a dotted eighth |
| 3 | `xx.x`, `...x`, `..xx`, `.xxx` |
| 4 | `.x..`, `.x.x`, `.xx.`: a sixteenth on the "e" after a held one |

The grades rise with how rarely a figure sounds intended: the plain values cost
nothing, the common sixteenth groups a little, the snap more, and a lone sixteenth on
the "e" or the "a" after a held note most. A held beat is free because
**syncopation** already charges a note attacked off the beat and held across this
one; the figure judges only where attacks fall inside the beat. A rest step counts as
an attack, since rests are not tied. Wall-to-wall sixteenths are held back twice:
each `xxxx` beat pays its grade, and a bar of sixteen plain sixteenths pays **rhythm**
as well. Two `xxxx` beats in a row pay nothing extra, since a run of sixteenths over
two beats (a scale, a broken chord) is idiomatic, above all over a slower bass.

The cost comes in three parts, over the ties of the beat's first two, three and four
steps: the least grade any figure starting so can still reach, then how much that
least rises with the third step, then the rest. The parts add up to the grade, never
go below zero, and read only ties, so the search weighs a figure tie by tie while it
decides the rhythm, before most pitches (a tie has two values and a pitch many), and
the optimizer's bound sees it early. Charged only on the beat's last tie, the cost
could not steer the first three: each would take the cheaper note on its own, and the
first piece would run in sixteenths until the last one.

Keys counted in steps (`length`, `delay`, `phase`, `modulate_at`, `rest_at`,
`max_hold`, `max_rests`) count grid steps, and their defaults are not rescaled: on the
eighth grid `max_hold 1` allows quarter notes at most and `max_rests 2` two eighth
rests, so `examples/eighths.txt` sets `max_hold 3` for half notes; on the sixteenth
grid `max_hold 3` allows a quarter note and 7 a half note, which
`examples/sixteenths.txt` sets. A melody is at most 128 steps (sixteen bars of
eighths, eight of sixteenths) and a piece 256. `tempo` counts quarter notes on every
grid. The score files write the grid's values (MusicXML with
two or four divisions to the quarter and types down to `16th`, LilyPond `8` or `16`
and dotted values, ABC `L:1/8` or `L:1/16` with the notes of each beat beamed), tie a
length no single value writes, such as five eighths or five sixteenths, and pad the
last bar to its eight or sixteen steps; MIDI gives a step 240 or 120 ticks, and
`proof.json` and the page data carry `stepsPerBeat` and `stepsPerBar`.

**Built.** The quarter, eighth and sixteenth grids, and the rhythm figures of the
sixteenth grid. **Not built**: figures for the eighth grid (two steps make only four
patterns, which syncopation and rhythm already cover), cadence type as a variable
(authentic, half, deceptive), phrase boundaries as variables, meter as a variable
(4/4 and the grid are fixed for the piece), per-voice meters (`poly_meter` changes
which steps are strong for every voice), and form as a state machine.

## 10. Search

- **Propagation**: generalized arc consistency over each constraint's scope, with the
  last support found cached per value; a counting propagator for the rest limit; a
  queue of constraints to revisit.
- **Backtracking** with snapshots on the heap (one per depth). A trail would use less
  memory; at up to about 320 variables (a 128-note melody with ties, a chord per
  bar and two keys), snapshots are simple and fast enough. A solver's own state, with
  the record of every decision, is about 280 KB at that size (most of it the 323
  decision records with their ranked candidates); `--sensitivity` keeps two of them
  on the stack under the model and a search up to 322 levels deep, close to 700 KB
  in all, so the Windows build asks for the 8 MB main-thread stack that Linux, macOS
  and the WebAssembly build already have rather than its default 1 MB.
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
WebAssembly in web workers. Its state has one owner each: the settings text is the
draft; a *piece* is what one run made of a draft (its text, variation, files and
proof); the engine's resolved settings (`--resolve`, then `proof.json`) are what the
controls show, so a value a preset sets, or a key given twice, reads as the engine
uses it. Every edit, from a control, the Players card or a what-if, goes through one
function that sets the key (its last line, as the engine applies, or the JSON member)
and either composes again or, for a key that keeps the notes, performs again and
hands the new performance to the page without reloading it. Every request and every
message from the page carries its run id; a newer request cancels an older one; a
failed run keeps the last good piece on screen marked as such, with no downloads for
the failed settings. Undo and redo move through composed pieces without composing
again. Hosted there, the inspector becomes a counterfactual tool: it offers every
other value of a melody note, and picking one fixes the note in the config (releasing
a lock on it) and composes again, reporting the notes that changed to fit or the rule
the value breaks. `tests/browser_test.mjs` drives these workflows in Chrome.

A fingerprint card draws the canon's coupling as an arc diagram: an arc for every pair
of melody notes heard at the same step in different voices, weighted by how long they
overlap and solid where a harmony rule removed values between them in the proof.

Under the tiles, a solved page sums up the collapse in plain words: the size of the
space before any rule (the product of the initial domains), the values removed and the
rules that removed the most, how many values were forced and chosen, the backtracks
before the first piece, and what the optimizer gained. In the demo, How many? runs
`--count` for up to four seconds, and the sheet music lights the bar being played.
`tests/web_smoke.mjs` checks the WebAssembly build the way the demo uses it.

**Players.** Each voice is a player (`parts.c` holds the one instrument table: names,
General MIDI programs, clefs, written transpositions, sounding ranges, and the
soundfont and level the page plays each with). An `ensemble` gives out instruments
by the voices' transpositions, highest first, never by the notes, so composing again
never swaps them; `parts` names any voice's instrument itself and wins. The model
makes each voice's range a rule: range_low..range_high narrowed by the instrument and
by `part_low` and `part_high`. A combination that cannot work is explained before the
search ("voice 1 (Flute, 60..96) and voice 2 (Tuba, ...) cannot play the same
melody"), and a note the range removes says which voice and instrument removed it.
The Players card in the page sets each voice's instrument, range, entry and
transposition (these compose again), its articulation and intensity (these perform
again, the notes kept) and its volume, pan, mute and solo (these change only what
you hear; the first two also go into canon.mid and voices.wav).

**Performance.** A piece played exactly as written sounds mechanical, so the engine
performs it, once, and every output plays that performance (`perform.c`): canon.mid
gets a tempo change wherever the time bends, each note's velocity and sounding length,
and each track's volume and pan; voices.wav renders the same timing and loudness on
synths (a pluck for struck or plucked parts, the organ tone for sustained ones); the
page plays the same plan on sampled instruments. The `mood` key names a profile
(`lament`, `hymn`, `triumph`, `longing`, `dance`, `nocturne`); `plain` is no shaping at
all, one tempo and one loudness, to compare with; `auto` guesses from the first key and
the tempo. A profile sets:

- a dynamic arc of three levels, at the start, at the climax and at the end, joined by
  cosine easing. The climax is the form's (`climax`), and each voice follows its own
  place in the melody, so a follower swells as it reaches the climax itself;
- metric accents, a swell across each phrase of the form, and a lift at each entry
  and on the subject's head wherever it is stated, scaled by each voice's `intensity`;
- the articulation (a dance detached, a hymn joined), unless a voice sets its own;
- one time map for all voices, so the canon keeps one pulse: rubato inside each phrase,
  a breath on the last beat of each phrase of the leading voice, and a ritardando over
  the last two bars (a loop keeps its own map without it);
- how long the final chord is held, as a fermata.

Nothing in it is random: a small per-note variation of loudness is a hash of the voice
and step, so a piece plays the same every time. The page schedules the plan ahead from
a timer, never playing a note late after a stalled background tab; a voice whose
samples cannot load plays its synth and the page says which; the bass line on the
chord roots is a separate playback option, off by default, outside the canon, the
proof and the files.

**Built.** **Partly built**: the fingerprint shows the coupling through simultaneous
notes; a full constraint graph (chords, keys, ties and every rule as nodes) is not
built.

## 13. Presets and modes

The presets are moods: `lament`, `hymn`, `triumph`, `longing`, `dance` and
`nocturne`. Each starts from the defaults and bundles an ensemble, a key and mode, a
tempo, rhythm and harmony, a form of two-bar phrases with the canon's entries on
phrase starts, the climax and the costs that make the line sound like its feeling. A
lament sighs downward by step and cries out early, on strings in D minor; a hymn moves
in long consonant steps on an organ, each phrase coming to rest; a triumph climbs a
fanfare in sequence to a late peak on brass; longing turns from A minor to C major and
peaks late; a dance is a quick round; a nocturne is a quiet piano duet that rises once.
Each preset also sets `mood`, so the score page performs it in kind. They are a
starting point to change with `--set`, not a guarantee of the feeling: the solver
weighs these costs against every rule, and how a piece moves a listener is not
measured.

**Built.** **Not built**: changing the rules part-way through a piece ("rule
mutation"), and separate physics, counterpoint and research modes. The outputs
already expose the research data: entropy, statistics, and removals by rule.

## 14. Implementation

- C11, no dependencies, builds warning-free with GCC, Clang and MSVC, and with
  Emscripten for the browser.
- Config: one table of keys drives text and JSON configs, `--set`, `--resolve`,
  validation, presets and [config.md](config.md). `report.txt` records every key used,
  so a piece can be reproduced from it. A list key (one value per voice, or the
  melody) changes only when the whole list is valid.
- Outputs are written beside their final names and moved into place when complete,
  so a failure never leaves part of a file; a run that finds no piece keeps the last
  piece's score files and says they are not its own.
- Inputs: text and JSON configs, and Standard MIDI files of format 0, 1 or 2
  (`midi_read.c`). `--melody-midi` reads a melody from the first track with notes,
  rounding onsets and ends to the nearest step (a quarter note, or an eighth or a
  sixteenth on the finer grids): a held note gives its pitch
  on every step it covers, the highest of overlapping notes wins, and a gap is a rest.
  `--corpus` counts the pitch classes of their notes as it does for lists of pitches;
  notes on channel 10, the General MIDI drums, are skipped.
- Outputs: MIDI, MusicXML, LilyPond, ABC, WAV, SVG, text and JSON traces, and the
  HTML page. The notation exports share one spelling (`key_spell`), cut notes at
  barlines and at the key change with ties, tie a length no single note value writes,
  and pad the last bar with a rest.
  With `ensemble`, `parts.c` gives each voice an instrument, ranked by average
  sounding pitch, and the exports write real parts: the instrument's name and clef,
  its General MIDI program on its MIDI track, and for a transposing instrument the
  written pitch, its interval above the sound, spelled in the written key (the
  sounding key's tonic moved by that interval, same mode). MusicXML adds the
  `<transpose>` that takes it back to the sound, LilyPond `\transposition`, ABC an
  inline key per voice. MIDI and WAV stay at sounding pitch. `written concert` keeps
  every part at sounding pitch, and `report.txt` counts notes outside each
  instrument's range.
  The WAV plays each note on a Karplus-Strong plucked string (or an organ of four
  harmonics, or a sine), pans the voices with equal power from left to right, and
  runs the mix through a small Schroeder reverb; the noise that excites each string
  is seeded from the note, so the same piece always gives the same file.

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
