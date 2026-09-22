# Gödel's Fugue
## Interpretable Symbolic Composition Through Constraint Propagation, State Collapse, and Mathematical Structure

> **Core idea:** A musical composition begins as a large space of unresolved possibilities. Every note, rhythm, harmony, key, and canon relationship is represented as a constrained state. Choosing one musical event reduces the set of legal future and past events. Those reductions propagate throughout the composition until the piece "collapses" into a fully determined musical object.

---

# 1. Project Vision

**Canon Collapse** is a symbolic music-composition engine built around the idea of **state collapse**.

The project is not a neural-network music generator and not a black-box generative AI system. Instead, it treats composition as a mathematical constraint problem.

A melody is initially unknown.

Each note position begins with a set of possible notes. A canon causes each chosen note to reappear later in shifted or transformed voices, so choosing a note at one position can constrain many other positions. Every musical rule—voice range, consonance, harmonic motion, melodic motion, suspensions, forbidden parallels, cadence requirements, scale membership, rhythmic structure, and more—acts as a mathematical constraint.

As the engine makes decisions, the global musical state shrinks.

A note can become forced because every alternative eventually violates one or more constraints.

When this happens, the program does not simply output the note. It emits a **proof trace** explaining exactly why the note became necessary.

Example:

```text
Beat 5 collapsed to E.

Initial domain:
{C, D, E, F, G}

C rejected:
- creates parallel octave between voices 1 and 2

D rejected:
- suspension at beat 4 fails to resolve downward

F rejected:
- forms forbidden tritone against delayed voice

G rejected:
- exceeds configured melodic leap limit

Remaining legal pitch:
E
```

The result is an **interpretable composition engine** where every note can have a causal explanation.

The larger goal is to explore a question:

> **Can musical composition be modeled as a hierarchy of unresolved states whose possibilities collapse through local decisions and globally propagating constraints?**

---

# 2. Why a Canon Is the Perfect Structure

A canon is unusually well suited to this idea because one melody is forced to coexist with shifted copies of itself.

Let the unknown melody be

\[
M = (x_0, x_1, x_2, \dots, x_{N-1})
\]

where each \(x_i\) is a pitch.

Suppose a second voice begins after a delay \(d\).

Then

\[
V_0(t) = M(t)
\]

and

\[
V_1(t) = M(t-d).
\]

For a three-voice canon:

\[
V_j(t)=M(t-jd).
\]

A single melody variable therefore participates in several different harmonic contexts.

For example, if \(d=4\),

```text
Voice 1: x0 x1 x2 x3 x4 x5 x6 x7 ...
Voice 2:             x0 x1 x2 x3 ...
Voice 3:                         x0 ...
```

At beat 4, the harmony contains:

\[
(x_4,x_0)
\]

At beat 8:

\[
(x_8,x_4,x_0)
\]

Therefore assigning

\[
x_0=C
\]

does much more than determine the first note.

That C also appears later in other voices and constrains the pitches sounding against it.

This creates **nonlocal propagation**.

A local decision can cause consequences far away in musical time.

---

# 3. The Core Mathematical Model

## 3.1 Variables

For a melody of length \(N\), define variables

\[
x_0,x_1,\dots,x_{N-1}.
\]

Each variable represents the pitch at one melodic position.

Initially,

\[
x_i \in D_i
\]

where \(D_i\) is its current domain.

For chromatic pitch classes,

\[
D_i \subseteq \{0,1,\dots,11\}.
\]

For MIDI pitches,

\[
D_i \subseteq \{0,\dots,127\}.
\]

In practice, a voice-range restriction might begin with

\[
D_i = \{60,61,\dots,72\}
\]

for C4 through C5.

---

## 3.2 State Space

The global state is the Cartesian product

\[
S=D_0\times D_1\times \cdots \times D_{N-1}.
\]

If every note initially has 12 possibilities,

\[
|S|=12^N.
\]

For \(N=16\),

\[
|S|=12^{16}.
\]

That is already approximately

\[
1.85\times 10^{17}
\]

possible pitch sequences before musical constraints are applied.

The engine does not enumerate this space directly.

Instead, it shrinks the domains through constraint propagation.

---

## 3.3 Collapse

A variable is fully collapsed when

\[
|D_i|=1.
\]

For example,

\[
D_5=\{C,D,E,F,G\}
\]

may shrink to

\[
D_5=\{D,E,G\},
\]

then

\[
D_5=\{E,G\},
\]

then

\[
D_5=\{E\}.
\]

At that point,

\[
x_5=E.
\]

This can happen because the solver explicitly chose E, or because constraints eliminated every other candidate.

The second case is a **forced collapse**.

---

# 4. Constraint Satisfaction Formulation

Canon Collapse can be modeled as a **Constraint Satisfaction Problem (CSP)**.

A CSP consists of:

1. Variables
2. Domains
3. Constraints

For Canon Collapse:

- Variables = notes, rhythms, harmonic states, keys, canon parameters, etc.
- Domains = currently legal possibilities
- Constraints = musical rules

A solution is a complete assignment satisfying all required constraints.

---

# 5. Hard and Soft Constraints

Not every musical rule should be absolute.

The engine should distinguish two categories.

## 5.1 Hard constraints

A hard constraint may never be violated.

Examples:

- voice exceeds configured range
- impossible duration
- illegal canon indexing
- explicitly forbidden parallel octave
- impossible suspension resolution
- two mutually exclusive structural states
- user-fixed note changed

If a candidate violates a hard constraint, it is removed from the domain.

---

## 5.2 Soft constraints

A soft constraint is allowed but penalized.

Examples:

- large melodic leap
- repeated notes
- excessive dissonance
- weak cadence
- too many identical rhythms
- awkward contour
- too much registral clustering

Soft constraints contribute to an energy or cost function.

This allows the program to generate music that is structured without being rigid.

---

# 6. Energy Function

Define a musical energy:

\[
E(s)=
w_1D(s)+
w_2P(s)+
w_3L(s)+
w_4R(s)+
w_5H(s)+
w_6C(s)+\cdots
\]

where:

- \(D\): dissonance cost
- \(P\): parallel-motion penalty
- \(L\): melodic leap penalty
- \(R\): rhythmic instability
- \(H\): harmonic instability
- \(C\): contour penalty
- \(w_i\): configurable weights

The solver can prefer lower-energy states while still permitting higher-energy states when musically desirable.

This creates an **energy landscape** over the space of compositions.

---

# 7. Musical Tension as Energy

The physics-inspired interpretation becomes stronger if tension is modeled explicitly.

A composition does not necessarily minimize energy monotonically.

Instead it can follow a designed tension curve:

```text
low → rising → high → release
```

For example, define a target tension function

\[
T^\*(t)
\]

over musical time.

The actual local tension might be

\[
T(t)=
\alpha D(t)
+\beta R(t)
+\gamma H(t)
+\delta L(t).
\]

Then the engine minimizes

\[
\sum_t |T(t)-T^\*(t)|.
\]

This lets the composer specify a dramatic shape.

Example:

```text
Measures 1–4: low tension
Measures 5–8: increasing instability
Measures 9–12: climax
Measures 13–16: resolution
```

---

# 8. Information and Entropy

A powerful mathematical quantity is the unresolved information in the system.

If note \(i\) has domain size \(|D_i|\), define approximate state entropy

\[
H=\sum_i\log_2|D_i|.
\]

This is not necessarily physical thermodynamic entropy. It is an information-theoretic measurement of unresolved possibilities.

If every variable is fully collapsed,

\[
H=0.
\]

If there are many possible assignments, \(H\) is large.

Example:

```text
Initial entropy:      61.7 bits
After key inference:  52.1 bits
After x3 = E:         45.8 bits
Propagation:          33.0 bits
Forced collapses:     12.4 bits
Completed piece:       0.0 bits
```

This gives the system a measurable notion of **state collapse**.

---

# 9. Information Gain

For an assignment \(a\),

\[
\Delta H = H_{\text{before}}-H_{\text{after}}.
\]

A decision with large \(\Delta H\) causes a large collapse of the possibility space.

The solver could prefer assignments that maximize expected information gain.

This produces a distinctive algorithmic strategy:

> Choose the musical decision that most strongly constrains the future.

That may produce more coherent and self-reinforcing music than choosing notes independently.

---

# 10. Minimum Remaining Values

A classic CSP heuristic is **Minimum Remaining Values**.

Choose the unresolved variable with the smallest domain.

If:

```text
x1 = {C,D,E,F,G}
x2 = {D,E}
x3 = {A}
x4 = {C,E,G}
x5 = {C,C#,D,E,F,G}
```

then \(x_3\) is already collapsed.

The next variable to examine is \(x_2\), because

\[
|D_2|=2.
\]

This is useful because highly constrained variables are the most likely to create contradictions.

---

# 11. Maximum Influence Heuristic

A more project-specific heuristic would be to measure how many other states a variable can influence.

Define an influence graph where nodes are musical variables and edges represent constraints.

Then choose

\[
x_i = \arg\max_i \text{Influence}(x_i).
\]

Possible definitions include:

\[
\text{Influence}(x_i)=\deg(x_i)
\]

or a weighted graph-centrality score.

This means the engine may choose the note whose collapse will affect the most other notes.

---

# 12. Expected Collapse Heuristic

For each candidate value \(v\in D_i\):

1. Tentatively assign \(x_i=v\)
2. Run local propagation
3. Measure resulting entropy
4. Undo the assignment

Then estimate

\[
\mathbb{E}[\Delta H \mid x_i].
\]

The solver can choose the variable or value that causes the greatest expected reduction in uncertainty.

This creates a principled definition of **musical collapse strength**.

---

# 13. Canon Transformations

A canon does not need to use identical shifted copies.

Define

\[
V_j(t)=T_j(M(t-d_j))
\]

where \(T_j\) is a transformation.

Possible transformations:

- identity
- transposition
- inversion
- retrograde
- augmentation
- diminution
- octave displacement
- pitch-class mapping
- diatonic transposition
- chromatic transposition

---

# 14. Transposition Canon

If voice \(j\) is transposed by \(k_j\) semitones,

\[
V_j(t)=M(t-d_j)+k_j.
\]

A single melody variable now appears in several pitch contexts.

This makes constraint propagation even richer.

---

# 15. Inversion Canon

Choose an inversion axis \(a\).

Define

\[
T(p)=2a-p.
\]

Modulo pitch class:

\[
T(p)\equiv 2a-p \pmod{12}.
\]

The second voice becomes a mirrored form of the melody.

This can create strong mathematical symmetry.

---

# 16. Retrograde Canon

A retrograde transformation can be defined by

\[
T(M)_i=M_{N-1-i}.
\]

A retrograde canon makes early and late melody decisions constrain each other.

This creates unusually strong backward propagation.

---

# 17. Rhythmic Augmentation

If a voice has augmentation factor \(k\),

\[
t' = kt.
\]

For \(k=2\), every duration doubles.

This allows a slow voice to coexist with the original melody.

---

# 18. Rhythmic Diminution

For

\[
0<k<1,
\]

the transformed voice moves faster than the source melody.

A strict integer grid can be used internally to avoid floating-point timing problems.

---

# 19. Phase-Shift Canons

Instead of delaying by an integer number of beats, allow fractional offsets.

Examples:

\[
d=\frac12,\frac32,\frac54,\dots
\]

This creates continuously changing vertical interactions.

The engine can operate on a fine rhythmic lattice such as sixteenth-note or thirty-second-note ticks.

---

# 20. Dynamic Canon Delay

The delay itself could be a state variable.

Instead of a fixed \(d\),

\[
d\in\{2,3,4,6,8\}.
\]

The system can collapse the canon delay based on which offsets produce valid musical relationships.

This makes the form itself part of the search.

---

# 21. Scale and Key as Unresolved State

Do not necessarily fix the key in advance.

Let the tonal state be

\[
K \in \mathcal{K}
\]

where

\[
\mathcal{K}=
\{
C\text{ major},
A\text{ minor},
D\text{ Dorian},
\dots
\}.
\]

Each new note provides evidence for or against these possibilities.

Example:

```text
Initial tonal states:
C major
A minor
D Dorian
E Phrygian
G Mixolydian
```

After several notes:

```text
C major        compatible
A minor        compatible
D Dorian       weakened
E Phrygian     eliminated
G Mixolydian   weakened
```

Eventually the context may force one interpretation.

---

# 22. Scale Collapse

Represent candidate scales as sets of pitch classes.

For example:

\[
C_{\text{maj}}=\{0,2,4,5,7,9,11\}.
\]

If a melody contains a pitch not in a candidate scale and chromaticism is disallowed, eliminate that scale.

A more flexible model assigns penalties rather than immediate rejection.

---

# 23. Tonal Probability

Instead of hard elimination, assign weights:

\[
P(K\mid \text{music}).
\]

Using a Bayesian-style interpretation,

\[
P(K\mid X)
\propto
P(X\mid K)P(K).
\]

The system can maintain several plausible tonal interpretations until one becomes dominant.

This creates **tonal superposition** in a mathematical—not quantum-mechanical—sense.

---

# 24. Modulation Graph

Represent keys as nodes in a graph.

Edges represent plausible modulations.

Examples:

- relative major/minor
- dominant
- subdominant
- parallel major/minor
- mediant relations
- chromatic-mediant relation
- common-tone modulation

If the current key is \(K_a\), a new key \(K_b\) may be allowed only if an edge exists:

\[
(K_a,K_b)\in E.
\]

This makes modulation a path through a tonal graph.

---

# 25. Modulation Cost

Assign edge weights:

\[
w(K_a,K_b).
\]

Closely related keys get low cost.

Remote keys get higher cost.

Then a sequence of modulations has total cost

\[
C=\sum_t w(K_t,K_{t+1}).
\]

The engine can seek musically coherent tonal trajectories.

---

# 26. Pivot-Chord Modulation

A modulation can require a chord shared by both keys.

Let

\[
\mathcal{C}(K)
\]

be the set of diatonic chords in key \(K\).

A pivot chord exists if

\[
\mathcal{C}(K_a)\cap\mathcal{C}(K_b)\neq\varnothing.
\]

The engine can emit proof traces such as:

```text
Modulation: C major → G major

Pivot chord:
A minor

Function in C major:
vi

Function in G major:
ii

Reason accepted:
- chord belongs to both tonal systems
- D major dominant follows naturally
```

---

# 27. Harmony Variables

The piece can maintain chord-level state variables.

Let

\[
h_t \in H_t
\]

where \(h_t\) might represent:

- I
- ii
- iii
- IV
- V
- vi
- vii°
- secondary dominants
- modal mixture
- altered chords

Pitch constraints then depend on harmony.

For example:

\[
x_t\in \text{ChordTones}(h_t)
\]

on strong beats.

---

# 28. Functional Harmony Constraints

Possible rules:

- dominant tends toward tonic
- predominant tends toward dominant
- leading tone tends upward
- chordal seventh tends downward
- cadential 6/4 resolves to V
- secondary dominants resolve to their targets

These rules can be strict or weighted.

---

# 29. Voice-Leading Constraints

Important rules may include:

- avoid parallel fifths
- avoid parallel octaves
- restrict direct perfect intervals
- resolve leading tones
- resolve sevenths downward
- treat suspensions correctly
- avoid voice crossing
- avoid excessive spacing
- restrict repeated large leaps
- compensate large leaps by contrary stepwise motion

These constraints are especially valuable because they produce compelling proof traces.

---

# 30. Parallel Fifth Detection

Suppose two voices have notes

\[
a_t,b_t
\]

at time \(t\), and

\[
a_{t+1},b_{t+1}
\]

at time \(t+1\).

A parallel perfect fifth occurs when:

\[
|a_t-b_t|\equiv 7\pmod{12}
\]

and

\[
|a_{t+1}-b_{t+1}|\equiv 7\pmod{12}
\]

and both voices move in the same direction.

This can be checked algorithmically.

---

# 31. Parallel Octave Detection

Similarly:

\[
a_t-b_t\equiv 0\pmod{12}
\]

and

\[
a_{t+1}-b_{t+1}\equiv 0\pmod{12}
\]

with similar motion.

---

# 32. Melodic Leap Cost

For successive pitches,

\[
\Delta_t=x_t-x_{t-1}.
\]

A simple leap penalty could be

\[
L_t=\max(0,|\Delta_t|-L_0)^2.
\]

For example, if intervals larger than a perfect fifth are discouraged,

\[
L_0=7.
\]

---

# 33. Melodic Momentum

Treat pitch as position.

Define melodic velocity:

\[
v_t=x_t-x_{t-1}.
\]

Define melodic acceleration:

\[
a_t=v_t-v_{t-1}.
\]

Then sudden contour changes can receive a cost.

For example:

\[
C_{\text{accel}}=\sum_t a_t^2.
\]

This favors smoother melodic motion.

---

# 34. Musical Gravity

Tonal music naturally contains pitch attractions.

Define a tonal potential:

\[
U(p,K).
\]

Stable notes receive low potential.

Unstable notes receive high potential.

For C major, one conceptual model might assign:

```text
C  low
E  low
G  low
D  medium
F  medium
A  medium
B  high
```

The leading tone B has high potential because it strongly tends toward C.

A resolution can be modeled as movement toward lower potential.

---

# 35. Potential Energy of a Phrase

For a phrase,

\[
U_{\text{phrase}}
=
\sum_t U(x_t,K_t).
\]

Cadential regions can be designed so that potential decreases sharply.

This produces a mathematical model of resolution.

---

# 36. Conservation-Law Inspired Constraints

The project can introduce useful global invariants.

These are not physical conservation laws, but they behave mathematically like them.

Example: registral center.

Let

\[
R=\frac1N\sum_i x_i.
\]

Require:

\[
|R-R^\*|<\epsilon.
\]

This prevents the melody from drifting too high or low.

---

# 37. Motion Balance

If one voice moves strongly upward, another may be encouraged to move downward.

Define total directional motion:

\[
M_t=\sum_j(V_j(t+1)-V_j(t)).
\]

A balancing rule might penalize large \(|M_t|\).

This encourages contrapuntal independence.

---

# 38. Symmetry

Symmetry can be a compositional constraint.

Possible symmetries:

- inversion symmetry
- retrograde symmetry
- rhythmic palindrome
- interval symmetry
- phrase-length symmetry
- axial symmetry around a pitch

For an inversion axis \(a\),

\[
x_i+x_{N-1-i}=2a.
\]

---

# 39. Controlled Symmetry Breaking

Perfect symmetry can sound sterile.

The system can intentionally satisfy symmetry for a while, then break it at a structural point.

Example:

```text
Measures 1–8:
strict inversion symmetry

Measure 9:
symmetry broken

Measure 12:
new asymmetric motif introduced
```

The breaking of symmetry itself becomes a compositional event.

---

# 40. Rhythm as State

Pitch should not be the only unresolved variable.

Let each event also have duration

\[
r_i\in R_i.
\]

Possible durations:

\[
R_i=
\left\{
\frac14,\frac12,1,2
\right\}
\]

beats.

The rhythm domains collapse alongside pitch domains.

---

# 41. Metric Strength

Define metric weight \(m(t)\).

Strong beats may prefer:

- chord tones
- consonances
- structural notes

Weak beats may permit:

- passing tones
- neighbors
- suspensions
- accented dissonances

This provides context-sensitive constraints.

---

# 42. Rhythm Entropy

Rhythmic uncertainty can also be measured:

\[
H_R=\sum_i\log_2|R_i|.
\]

Total system entropy might be

\[
H_{\text{total}}
=
H_{\text{pitch}}
+
\lambda_R H_R
+
\lambda_K H_K
+
\lambda_H H_H.
\]

This creates a genuinely hierarchical state-space model.

---

# 43. Hierarchical Collapse

The most important architectural concept is that collapse can happen at several levels.

## Level 1: note state

```text
Beat 7 pitch:
{C,D,E,F,G}
```

## Level 2: harmonic state

```text
Beat 7 harmony:
{ii, IV, V, vi}
```

## Level 3: tonal state

```text
Current key:
{D minor, F major, G Dorian}
```

## Level 4: structural state

```text
Phrase ending:
{half cadence, deceptive cadence, authentic cadence}
```

## Level 5: canon state

```text
Voice-2 delay:
{2,4,6 beats}
```

A low-level note decision may eliminate a harmony.

That harmony elimination may eliminate a key.

That key elimination may force a modulation.

Thus:

```text
note
  ↓
harmony
  ↓
key
  ↓
section
  ↓
global form
```

This is one of the strongest ideas in the entire project.

---

# 44. Proof Traces

Every elimination should have an explanation object.

Example:

```text
candidate: F4
status: rejected

reason:
parallel_fifth

participants:
voice_1 beat_12 = F4
voice_2 beat_12 = Bb3
voice_1 beat_13 = G4
voice_2 beat_13 = C4
```

The human-readable renderer converts this to:

```text
F4 rejected at beat 13:
- would create parallel perfect fifths
- voice 1: F4 → G4
- voice 2: Bb3 → C4
```

---

# 45. Proof DAG

A simple log is useful, but a **proof graph** is more powerful.

Each fact becomes a node.

Example:

```text
x3 = C
   ↓
x7 ≠ C
   ↓
x11 ∈ {E,G}
   ↓
x15 = E
   ↓
x11 = G
```

This forms a directed acyclic graph of implications.

A user can click any final note and ask:

> Why is this note here?

The engine traverses the proof graph backward.

---

# 46. Minimal Explanations

A proof can be large.

The engine should attempt to show a minimal relevant explanation.

If 20 constraints were checked but only three are necessary to prove a note was forced, display those three.

This makes the proof system readable.

---

# 47. Contradiction Certificates

Sometimes an assignment leads to no valid composition.

Instead of silently backtracking, record the contradiction.

Example:

```text
Contradiction detected.

Assumptions:
- beat 4 = F#
- beat 8 = C
- strict canon delay = 4
- no parallel fifths
- leading tones must resolve

Conflict:
No legal value remains for beat 12.
```

---

# 48. Unsatisfiable Cores

A sophisticated extension is to find a small subset of constraints that caused the contradiction.

In SAT/SMT terminology this resembles an **unsat core**.

Example:

```text
Minimal conflicting rule set:

1. beat 4 fixed to F#
2. beat 8 fixed to C
3. canon delay = 4
4. forbid parallel fifths

Removing any one of these restores at least one solution.
```

This is extremely valuable for explainability.

---

# 49. Counterfactual Explanation

Allow the user to ask:

> What if beat 9 were F instead?

The engine temporarily applies the change and measures the consequences.

Example:

```text
Counterfactual:
beat 9 = F

Result:
valid

Required downstream changes:
beat 13: E → G
beat 17: C → D
beat 21: G → A

Energy:
original: 18.7
counterfactual: 22.1

Entropy after propagation:
original: 10.3 bits
counterfactual: 14.6 bits
```

This shows how structurally important a note is.

---

# 50. Structural Sensitivity

For every note \(x_i\), perturb it and measure the downstream effect.

Define

\[
S_i=
\frac{\text{number of changed dependent states}}
{\text{total states}}.
\]

Large \(S_i\) means the note is structurally important.

This can produce a heatmap over the score.

---

# 51. Bifurcation Points

A decision is especially interesting if different choices create radically different futures.

Suppose:

```text
x8 = C  → 18,402 valid completions
x8 = C# → 91 valid completions
x8 = D  → 0 valid completions
```

Beat 8 is a **bifurcation point**.

The UI can highlight these moments.

---

# 52. Number of Completions

For small enough problems, the solver may count valid completions.

Define

\[
N(x_i=v)
\]

as the number of full solutions consistent with \(x_i=v\).

This produces a direct measure of how restrictive each choice is.

Exact counting may be expensive, so approximate counting can be used for larger pieces.

---

# 53. Temperature

The project can borrow a mathematical idea from statistical mechanics.

Given energy \(E(s)\), sample states with probability

\[
P(s)\propto e^{-E(s)/T}.
\]

where \(T\) is temperature.

At low temperature:

- conservative
- low-energy
- predictable
- rule-following

At high temperature:

- exploratory
- unstable
- surprising
- willing to accept higher-cost states

---

# 54. Simulated Annealing

The temperature can change during generation.

Start high:

\[
T_0 \gg 0
\]

and gradually cool:

\[
T_{n+1}=\alpha T_n
\]

with

\[
0<\alpha<1.
\]

This produces:

```text
exploration → stabilization → convergence
```

Musically:

```text
opening → development → resolution
```

---

# 55. Reverse Annealing

A composition could intentionally become less stable.

Increase temperature over time.

This could create:

```text
stable opening
→ increasing rhythmic instability
→ chromaticism
→ structural breakdown
```

Useful for experimental pieces.

---

# 56. Metastability

A musical state may be locally stable without being globally optimal.

The engine may remain in a region of the search space because nearby alternatives are worse.

Later a structural constraint forces a transition.

This creates a meaningful analogy to metastable systems.

Musically, this may correspond to:

- prolonged dominant
- ambiguous harmony
- pedal tone
- repeated ostinato
- unresolved suspension field

---

# 57. Motif Variables

Motifs can themselves become constrained objects.

Define a motif:

\[
m=(\Delta_1,\Delta_2,\dots,\Delta_k)
\]

where \(\Delta_i\) are intervals.

The engine can require transformed recurrences.

Examples:

- transposed motif
- inverted motif
- augmented rhythm
- fragmented motif

---

# 58. Emergent Motif Detection

Do not always specify motifs manually.

After generation, scan the interval sequence for repeated patterns.

If the interval sequence is

\[
(+2,+2,-1,+2,+2,-1,\dots)
\]

the system may identify:

```text
Emergent motif:
+2, +2, -1

Occurrences:
7

Explicitly programmed:
no
```

This is interesting because structure can emerge from constraints alone.

---

# 59. Motif Pressure

Once an emergent motif becomes common, the engine can optionally treat it as a new soft constraint.

This creates **self-reinforcing composition**.

The system discovers a pattern and then begins favoring it.

---

# 60. Rule Presets

The same engine can operate under different musical "laws."

Possible presets:

### Renaissance
- strict consonance treatment
- controlled dissonance
- smooth stepwise lines
- limited chromaticism

### Baroque
- functional tonality
- stronger dominant-tonic motion
- suspensions
- sequences

### Classical
- periodic phrasing
- cadential clarity
- balanced harmonic rhythm

### Minimalist
- repetition
- phase shift
- slow transformation
- limited harmonic vocabulary

### Experimental
- relaxed consonance
- symmetry
- mathematical transforms
- unusual meter

These are not meant to perfectly emulate historical style. They are configurable rule systems.

---

# 61. Rule Mutation

A more experimental feature:

During composition, allow the rule set itself to change.

Example:

```text
Section A:
parallel fifths forbidden

Section B:
parallel fifths allowed with penalty

Section C:
parallel fifths encouraged
```

This lets the program explore how changing laws changes the resulting music.

---

# 62. "Physics Mode"

A dedicated mode can emphasize the physics-inspired elements:

- entropy display
- energy display
- temperature
- state space
- metastability
- symmetry breaking
- conservation-inspired constraints
- collapse events

The UI should be careful to describe these as **mathematical analogies**, not literal quantum mechanics.

---

# 63. "Counterpoint Mode"

A second mode emphasizes traditional music-theory constraints:

- species counterpoint rules
- suspensions
- consonance classes
- forbidden parallels
- range
- melodic contour
- cadence rules

---

# 64. "Research Mode"

A third mode exposes detailed internal data:

- domain sizes
- propagation counts
- constraint graph
- proof DAG
- energy values
- entropy
- backtracking depth
- branch factor
- number of candidate solutions

This makes the engine useful as an educational and research visualization.

---

# 65. Human Intervention

The user should be able to freeze any state.

Example:

```text
User locks:
beat 8 = F#
```

The system treats that as a hard constraint.

It then re-propagates the entire composition.

---

# 66. Interactive Re-Collapse

The strongest interactive demo is:

1. Generate a valid canon
2. User clicks one note
3. Change it
4. Re-run propagation
5. Watch surrounding states change

Example:

```text
User mutation:
beat 12
E → F

Propagation:
beat 16 domain: {C,D,E,G} → {D,G}
beat 20: G becomes illegal
harmonic state 5: V eliminated
key candidate: A minor eliminated
section cadence: authentic → deceptive
```

This visually demonstrates nonlocal dependency.

---

# 67. Locked vs Flexible Notes

A note can have different statuses:

```text
FREE
SELECTED
FORCED
USER_LOCKED
BACKTRACKED
INVALID
```

These can have different UI treatments.

---

# 68. Explainability Levels

Offer several explanation depths.

### Simple

```text
E was chosen because every other pitch violated a rule.
```

### Intermediate

```text
C creates parallel octaves.
D fails the suspension resolution.
F creates a tritone.
G exceeds the voice range.
```

### Formal

```text
Constraint C_parallel_octave(x5=C) = false
Constraint C_suspension(x5=D) = false
Constraint C_tritone(x5=F) = false
Constraint C_range(x5=G) = false
Therefore D5 = {E}.
```

---

# 69. Constraint Graph

Represent the CSP as a graph.

Nodes:

- pitch variables
- rhythm variables
- harmony variables
- key variables
- structural variables

Edges:

- binary constraints
- dependency links
- canon identity relationships
- transformation relationships

Hyperedges may represent constraints involving more than two variables.

---

# 70. Hypergraph Representation

Many musical rules involve several states simultaneously.

A cadence may depend on:

\[
(h_{t-2},h_{t-1},h_t)
\]

A parallel fifth depends on four pitches.

Therefore a hypergraph may be more accurate than a simple graph.

A constraint

\[
C(x_1,x_2,x_3,x_4)
\]

creates a hyperedge connecting all four variables.

---

# 71. Arc Consistency

For binary constraints, the engine can use arc-consistency algorithms.

For variables \(X\) and \(Y\), every value in \(D_X\) must have at least one compatible value in \(D_Y\).

If not, remove it.

This is the principle behind algorithms like AC-3.

---

# 72. AC-3 Style Propagation

Pseudo-code:

```text
queue = all constraint arcs

while queue not empty:
    (X,Y) = pop(queue)

    if revise(X,Y):
        if domain(X) is empty:
            contradiction

        for each neighbor Z of X:
            push(Z,X)
```

This gives a mathematically clean core solver.

---

# 73. Generalized Propagation

Because many music constraints are higher-order, implement a generic constraint interface:

```c
bool propagate(
    Constraint *constraint,
    SolverState *state,
    ProofLog *proof
);
```

Each constraint inspects relevant variables and removes unsupported values.

---

# 74. Backtracking Search

Propagation will not solve every problem automatically.

When several possibilities remain:

1. choose a variable
2. choose a value
3. assign it
4. propagate
5. if contradiction occurs, backtrack

Pseudo-code:

```text
solve(state):
    propagate(state)

    if contradiction:
        return failure

    if all variables collapsed:
        return success

    X = choose_variable(state)

    for value in order_values(X):
        child = copy(state)
        assign(child, X, value)

        if solve(child):
            return success

    return failure
```

---

# 75. Backjumping

Basic backtracking reverses one decision at a time.

A stronger system can identify which earlier assignment caused the contradiction and jump directly back to it.

This is called conflict-directed backjumping.

It fits extremely well with the proof system.

---

# 76. Nogood Learning

When the solver discovers that a combination of assignments cannot work, record it.

Example:

```text
NOT:
x3 = C
AND x7 = F#
AND delay = 4
```

If the solver encounters this combination again, it can reject it immediately.

This is analogous to clause learning in SAT solving.

---

# 77. SAT/SMT Backend Possibility

A future version could encode constraints for an SMT solver such as Z3.

Examples:

- integer pitch variables
- modular pitch-class relations
- inequality range constraints
- logical implication
- disjunction
- arithmetic interval constraints

However, the first version can be implemented directly in C for educational clarity.

---

# 78. Why C Is a Good Choice

C reinforces the project's identity.

Advantages:

- full control over solver state
- efficient bitsets for pitch domains
- explicit memory management
- fast propagation
- easy proof instrumentation
- easy integration with MIDI libraries
- stronger systems-programming angle

A C implementation also differentiates the project from typical Python generative-music experiments.

---

# 79. Pitch Domain Bitsets

For pitch classes, a 16-bit integer can represent all 12 possibilities.

Example:

```c
typedef uint16_t PitchMask;
```

If bit \(p\) is set, pitch class \(p\) is legal.

Example:

```c
#define NOTE_C  (1u << 0)
#define NOTE_CS (1u << 1)
#define NOTE_D  (1u << 2)
...
```

A C-major domain is:

```c
NOTE_C |
NOTE_D |
NOTE_E |
NOTE_F |
NOTE_G |
NOTE_A |
NOTE_B
```

---

# 80. Domain Operations

Useful operations:

```c
bool domain_contains(PitchMask domain, int pitch);
PitchMask domain_remove(PitchMask domain, int pitch);
int domain_count(PitchMask domain);
bool domain_singleton(PitchMask domain);
int domain_value(PitchMask domain);
```

Bitsets make these operations extremely fast.

---

# 81. MIDI Pitch Domains

For full MIDI pitch ranges, use a larger bitset.

For example:

```c
typedef struct {
    uint64_t bits[2];
} MidiDomain;
```

This gives 128 bits.

---

# 82. Core Variable Structure

Possible design:

```c
typedef enum {
    VAR_PITCH,
    VAR_RHYTHM,
    VAR_HARMONY,
    VAR_KEY,
    VAR_STRUCTURE
} VariableType;

typedef struct {
    int id;
    VariableType type;
    Domain domain;
    int assigned;
    int value;
} Variable;
```

---

# 83. Constraint Interface

```c
typedef enum {
    CONSTRAINT_CANON,
    CONSTRAINT_RANGE,
    CONSTRAINT_SCALE,
    CONSTRAINT_PARALLEL_FIFTH,
    CONSTRAINT_PARALLEL_OCTAVE,
    CONSTRAINT_MELODIC_LEAP,
    CONSTRAINT_HARMONY,
    CONSTRAINT_CADENCE,
    CONSTRAINT_SUSPENSION
} ConstraintType;

typedef struct Constraint Constraint;

struct Constraint {
    int id;
    ConstraintType type;
    int variable_ids[8];
    int variable_count;

    bool (*propagate)(
        Constraint *,
        SolverState *,
        ProofLog *
    );
};
```

---

# 84. Proof Event Structure

```c
typedef struct {
    int id;
    int variable_id;
    int removed_value;
    int constraint_id;

    int parent_events[8];
    int parent_count;

    char message[256];
} ProofEvent;
```

This naturally builds a proof DAG.

---

# 85. Solver State

```c
typedef struct {
    Variable *variables;
    size_t variable_count;

    Constraint *constraints;
    size_t constraint_count;

    ProofLog proof;

    double energy;
    double entropy;

    int contradiction;
} SolverState;
```

---

# 86. Propagation Queue

Use a queue of constraints affected by changes.

When a variable domain shrinks, enqueue neighboring constraints.

This avoids reevaluating every rule after every change.

---

# 87. Dependency Index

Maintain:

```text
variable → list of constraints
```

Example:

```c
typedef struct {
    int constraint_ids[MAX_NEIGHBORS];
    int count;
} ConstraintAdjacency;
```

This is essential for efficient propagation.

---

# 88. Decision Stack

Backtracking requires recording decisions.

```c
typedef struct {
    int variable_id;
    int chosen_value;
    int alternative_mask;
    size_t proof_checkpoint;
} Decision;
```

A stack stores the search path.

---

# 89. Copy vs Trail

Two implementation strategies:

## State copying

Copy the full solver state at each branch.

Simpler, but more expensive.

## Trail-based undo

Record changes:

```text
variable 5:
domain old = 0x2AF
domain new = 0x08A
```

Undo them when backtracking.

This is more efficient and more SAT-solver-like.

---

# 90. Trail Structure

```c
typedef struct {
    int variable_id;
    Domain old_domain;
} TrailEntry;
```

Before modifying a domain, push the old value.

At backtrack, restore until reaching the prior checkpoint.

---

# 91. Value Ordering

When choosing among pitches, possible heuristics include:

- minimum energy
- maximum entropy reduction
- smallest melodic leap
- strongest harmonic fit
- random weighted sampling
- temperature-based sampling

The user can select the heuristic.

---

# 92. Deterministic Mode

For debugging:

```text
seed = fixed
value ordering = deterministic
constraint order = deterministic
```

The same settings always produce the same composition.

This is crucial for reproducibility.

---

# 93. Stochastic Mode

For artistic exploration:

```text
seed = random
temperature > 0
weighted value ordering
```

The same rules can produce many different valid compositions.

---

# 94. Reproducibility

Every generated piece should save:

```text
seed
rule preset
weights
canon transformation
voice count
delay
temperature schedule
solver version
```

This gives each composition a reproducible configuration.

---

# 95. Output Formats

Useful outputs:

- MIDI
- MusicXML
- plain text
- JSON solver trace
- SVG score rendering
- WAV via external synthesizer
- CSV statistics

MIDI should be the first target.

MusicXML can come later.

---

# 96. Project File Format

Define a JSON-like configuration:

```json
{
  "length": 32,
  "voices": 3,
  "canon_delay": 4,
  "transform": "identity",
  "pitch_range": [60, 76],
  "scale_mode": "dynamic",
  "forbid_parallel_fifths": true,
  "forbid_parallel_octaves": true,
  "max_melodic_leap": 9,
  "temperature": 0.15,
  "seed": 42198
}
```

---

# 97. Piece Metadata

Each composition can export a report:

```text
Title:
Canon Collapse #42198

Variables:
32 pitch
32 rhythm
8 harmony
3 tonal

Constraints:
412

Propagation events:
7,218

Forced collapses:
19

Search decisions:
13

Backtracks:
5

Peak entropy:
74.3 bits

Final energy:
17.8

Generation time:
23 ms
```

---

# 98. Visualization: Score + State

A strong UI layout:

```text
┌─────────────────────────────────────┐
│               SCORE                 │
│                                     │
│   notes shown as normal notation    │
│                                     │
└──────────────────┬──────────────────┘
                   │
┌──────────────────┴──────────────────┐
│            STATE INSPECTOR          │
│                                     │
│ Beat 17                             │
│ Domain: {D,F,A,C}                   │
│ Entropy: 2.00 bits                  │
│ Energy contribution: 0.74           │
│                                     │
│ Constraints:                        │
│ - canon offset                      │
│ - harmony                           │
│ - melodic leap                      │
│ - parallel fifth                    │
└─────────────────────────────────────┘
```

---

# 99. Collapse Animation

When propagation occurs, visually animate domain reduction.

Example:

```text
Beat 12

{C,D,E,F,G,A}
      ↓
{C,D,E,G}
      ↓
{D,E,G}
      ↓
{E,G}
      ↓
{E}
```

A small pulse can travel along dependency edges.

---

# 100. Constraint Graph View

Display variables as nodes.

Use edges to show dependencies.

When the user changes one note, animate:

```text
x4
 ↓
x8
 ↓
harmony_3
 ↓
key_state
 ↓
cadence_state
```

This makes global propagation visually obvious.

---

# 101. Proof Inspector

Click a note and display:

```text
WHY E4?

E4 became forced at solver step 271.

C4 rejected:
parallel octave

D4 rejected:
unresolved suspension

F4 rejected:
forbidden tritone

G4 rejected:
melodic range violation

Remaining value:
E4
```

Include a button:

```text
SHOW FULL PROOF
```

---

# 102. Counterfactual Inspector

A second button:

```text
WHAT IF?
```

Select an alternative note.

The system simulates the resulting branch without destroying the original.

---

# 103. Entropy Graph

Plot entropy over solver steps:

```text
H
│\
│ \
│  \__
│     \__
│        \____
└────────────── step
```

Interesting spikes or plateaus may correspond to backtracking or ambiguous regions.

---

# 104. Energy Graph

Plot musical energy over time.

This is different from solver entropy.

Entropy answers:

> How unresolved is the composition?

Energy answers:

> How tense or costly is the current musical state?

Those should remain separate concepts.

---

# 105. Collapse Density

Define the number of forced collapses caused by each decision.

If assigning \(x_i\) forces five other states:

\[
C_i=5.
\]

Visualize \(C_i\) under each note.

This reveals which decisions have the strongest causal impact.

---

# 106. Dependency Distance

Measure how far in musical time a decision propagates.

If beat 2 affects beat 30, the decision has large temporal reach.

Define:

\[
R_i=\max_j |j-i|
\]

over affected variables \(j\).

---

# 107. Canon Self-Interaction Matrix

A useful visualization is a matrix showing which melody positions interact because of canon offsets.

Rows and columns correspond to melody positions.

Mark a cell when two positions sound simultaneously in some pair of voices.

This makes the self-constraint structure visible.

---

# 108. Constraint Matrix

Construct

\[
A_{ij}
\]

where \(A_{ij}=1\) if \(x_i\) and \(x_j\) participate in a shared constraint.

This creates an adjacency matrix.

Patterns may emerge based on canon delay.

---

# 109. Mathematical Analysis of Delay

Different canon delays produce different interaction graphs.

For delay \(d\), edges often connect variables separated by multiples of \(d\).

This suggests graph-theoretic analysis.

Questions:

- Which delays create the densest interaction graph?
- Which delays produce disconnected components?
- Which delays maximize constraint propagation?
- Which delays produce the fewest valid compositions?

This could become a research experiment within the project.

---

# 110. Greatest Common Divisor Effects

Suppose melody length is \(N\) and delay is \(d\).

The quantity

\[
\gcd(N,d)
\]

may influence cyclical interaction patterns if the canon wraps around.

If cyclic indexing is used,

\[
x_{(i+d)\bmod N},
\]

then the delay partitions positions into cycles.

The number of cycles is

\[
\gcd(N,d).
\]

Each cycle has length

\[
\frac{N}{\gcd(N,d)}.
\]

This is a beautiful direct connection between number theory and musical structure.

---

# 111. Cyclic Canon

A circular canon can use modular indexing:

\[
V_j(t)=M((t-jd)\bmod N).
\]

Now the piece has no fixed beginning or end.

Every melody position eventually interacts with every point in its delay cycle.

This can create mathematically elegant closed systems.

---

# 112. Modular Pitch Arithmetic

Pitch classes naturally use modular arithmetic.

\[
p\in\mathbb{Z}_{12}.
\]

Intervals are:

\[
\Delta = (p_2-p_1)\bmod 12.
\]

Transposition:

\[
T_k(p)=p+k\pmod{12}.
\]

Inversion:

\[
I_a(p)=2a-p\pmod{12}.
\]

This gives the project a clean algebraic foundation.

---

# 113. Group-Theoretic Extension

Transpositions and inversions form the dihedral group acting on pitch classes.

The set of operations

\[
T_n,\ I_n
\]

can be explored algebraically.

This is optional, but could add serious mathematical depth.

---

# 114. Transform Composition

Two transformations can be composed.

For example:

\[
T_a(T_b(p))=T_{a+b}(p).
\]

Inversion and transposition interact according to group rules.

The engine can represent transformations as formal operators instead of ad hoc code.

---

# 115. Pitch-Class Sets

Harmony can be modeled using pitch-class sets.

A chord is a subset

\[
C\subseteq \mathbb{Z}_{12}.
\]

Example:

\[
C_{\text{major}}=\{0,4,7\}.
\]

A voice's note is harmonically compatible if

\[
x_t\bmod 12\in C.
\]

---

# 116. Set Similarity

Two harmonies can be compared by set overlap.

Jaccard similarity:

\[
J(A,B)=\frac{|A\cap B|}{|A\cup B|}.
\]

This can help evaluate smooth harmonic transitions.

---

# 117. Voice-Leading Distance

For chords represented by pitch vectors,

\[
C_t=(p_1,\dots,p_n)
\]

and

\[
C_{t+1}=(q_1,\dots,q_n),
\]

define voice-leading distance:

\[
D(C_t,C_{t+1})
=
\sum_i |q_i-p_i|.
\]

The engine can favor parsimonious harmonic motion.

---

# 118. Form as a State Machine

The composition's large-scale form can be represented as a finite-state machine.

Example:

```text
INTRO
  ↓
DEVELOPMENT
  ↓
CLIMAX
  ↓
RESOLUTION
```

Transitions may depend on:

- elapsed time
- entropy
- energy
- harmonic state
- motif saturation

---

# 119. Formal State Machine

Let form state

\[
F_t\in
\{
I,D,C,R
\}.
\]

Define allowed transitions:

\[
I\to D,\quad
D\to D,\quad
D\to C,\quad
C\to R.
\]

Musical rule weights can depend on \(F_t\).

For example, climax mode may permit higher dissonance.

---

# 120. Adaptive Rule Weights

Instead of fixed \(w_i\),

\[
w_i=w_i(t,F_t).
\]

Example:

```text
INTRO:
dissonance penalty = 5.0

DEVELOPMENT:
dissonance penalty = 2.0

CLIMAX:
dissonance penalty = 0.8

RESOLUTION:
dissonance penalty = 6.0
```

Thus the same pitch can have different costs depending on form.

---

# 121. Cadence State

Cadence type can itself be unresolved.

\[
C_{\text{end}}
\in
\{
PAC,
IAC,
HC,
DC
\}.
\]

As final notes and harmonies collapse, incompatible cadence types are eliminated.

Eventually the cadence is determined.

---

# 122. Phrase Boundary Variables

Phrase boundaries need not be fixed.

Let

\[
b_t\in\{0,1\}
\]

indicate whether beat \(t\) ends a phrase.

Constraints may depend on:

- long note
- harmonic resolution
- melodic rest
- motif completion
- rhythmic slowdown

This makes phrasing emergent.

---

# 123. Dynamic Meter

Meter can also be a state.

\[
M\in
\{
3/4,4/4,6/8,5/4
\}.
\]

Rhythmic decisions can eliminate incompatible meters.

A future advanced version could allow meter changes.

---

# 124. Polymeter

Different canon voices could use different grouping while sharing the same underlying pulse.

This creates mathematically complex alignment patterns.

---

# 125. Polyrhythm

Examples:

- 3:2
- 4:3
- 5:4

Constraints can operate on a least-common-multiple tick grid.

For rhythms \(a\) and \(b\), the alignment period is related to

\[
\operatorname{lcm}(a,b).
\]

---

# 126. LCM Structure

If one voice repeats every 3 beats and another every 4 beats, their pattern realigns every

\[
\operatorname{lcm}(3,4)=12
\]

beats.

This gives another number-theoretic structure for the project.

---

# 127. Constraint Strength

A useful statistic for a constraint \(C\):

\[
S(C)=
\frac{\text{values eliminated by }C}
{\text{times }C\text{ evaluated}}.
\]

This measures how influential each musical rule is.

---

# 128. Rule Impact Report

After generation:

```text
Constraint impact:

Scale membership          1,204 eliminations
Voice range                 932
Parallel fifths             412
Parallel octaves            271
Melodic leap                208
Suspension resolution       117
Cadence constraints          84
```

This reveals what actually shaped the composition.

---

# 129. Constraint Competition

Sometimes two rules pull in different directions.

Example:

- harmony prefers G
- melodic smoothness prefers E

The engine can report this conflict.

```text
Beat 14 candidate scores:

E:
harmony       +2.2
melodic       -0.1
canon         +0.3
total          2.4

G:
harmony       -0.5
melodic       +1.4
canon         +0.2
total          1.1
```

---

# 130. Explainable Scoring

For soft constraints, explanations should show additive score contributions.

This avoids mysterious "AI confidence."

The user can see the exact numerical reasons.

---

# 131. Rule Weight Tuning

The UI can expose sliders:

```text
Dissonance             [----|-----]
Melodic smoothness      [-------|--]
Canon strictness        [---------|]
Motif repetition        [---|------]
Chromaticism            [------|---]
```

Changing a slider and regenerating can demonstrate how mathematical weights affect style.

---

# 132. Learned Weights

A future version could infer weights from example compositions.

Given pieces \(X_1,\dots,X_n\), estimate weights \(w\) such that the examples have lower energy than random alternatives.

This becomes an inverse problem.

---

# 133. Rule Discovery

Instead of only composing, the system can analyze music.

Given a score:

1. measure constraint violations
2. estimate rule weights
3. identify recurring structures
4. infer tonal states
5. detect motifs
6. build proof-like explanations

This creates a second project mode: **explainable music analysis**.

---

# 134. Style Fingerprints

For a corpus, compute statistics:

```text
average leap
parallel perfect interval frequency
chromatic note rate
cadence distribution
entropy profile
motif repetition
harmonic transition frequencies
```

These form a style fingerprint.

---

# 135. Search as Composition

The philosophical idea of Canon Collapse is:

> Composition is not the act of choosing one note after another. It is the process of reducing a structured possibility space until a coherent object remains.

This perspective unifies:

- music theory
- combinatorics
- graph theory
- constraint solving
- information theory
- optimization
- symbolic AI
- physics-inspired state models

---

# 136. Important Scientific Framing

The project should **not** claim that musical notes are literally quantum states.

Use careful language:

Good:

> "The engine borrows the language of state collapse as a mathematical analogy for constraint propagation."

Good:

> "The system tracks unresolved musical possibilities and collapses them through deterministic or probabilistic constraints."

Avoid:

> "The notes are quantum particles."

Avoid:

> "The program simulates wave-function collapse."

Unless an actual quantum-computing model is added later.

---

# 137. Minimal Viable Project

The first working version should be much smaller than the final vision.

### MVP goal

Generate an 8–16 note two-voice canon.

Features:

- fixed key
- fixed rhythmic grid
- fixed canon delay
- fixed pitch range
- note domains
- canon interaction
- consonance rule
- melodic leap rule
- parallel fifth rule
- parallel octave rule
- backtracking
- proof log
- MIDI output

Do not start with dynamic harmony, modulation, or UI.

---

# 138. MVP Mathematical State

Variables:

\[
x_0,\dots,x_{15}
\]

Domains:

\[
D_i\subseteq\{60,\dots,72\}
\]

Canon:

\[
V_1(t)=x_t
\]

\[
V_2(t)=x_{t-4}
\]

Constraints:

1. scale membership
2. range
3. maximum leap
4. vertical consonance
5. parallel fifths
6. parallel octaves

---

# 139. MVP Generation Loop

```text
initialize domains

apply fixed rules

while unresolved variables remain:

    propagate all constraints

    if contradiction:
        backtrack

    if forced variable exists:
        record collapse
        continue

    choose most constrained variable

    choose candidate pitch

    assign candidate

export MIDI
export proof
```

---

# 140. MVP Proof Example

```text
COLLAPSE EVENT 18

Variable:
x7

Domain before:
{62,64,65,67}

62 removed:
would form parallel fifth against delayed voice

64 removed:
creates melodic leap of 10 semitones

65 removed:
forms disallowed second on strong beat

Remaining:
67

Therefore:
x7 = G4
```

---

# 141. Phase 2

Add:

- three voices
- transformed canons
- energy scoring
- entropy
- stochastic value ordering
- temperature
- proof DAG
- counterfactuals

---

# 142. Phase 3

Add:

- harmony variables
- tonal-state inference
- dynamic modulation
- cadence states
- tension curves
- MusicXML export
- visual score
- web UI

---

# 143. Phase 4

Add:

- unsat cores
- learned weights
- corpus analysis
- motif discovery
- cyclic canons
- graph-theoretic experiments
- automated research reports

---

# 144. Suggested Repository Structure

```text
canon-collapse/
│
├── README.md
├── docs/
│   ├── math.md
│   ├── music-theory.md
│   ├── solver.md
│   └── proof-system.md
│
├── src/
│   ├── main.c
│   ├── solver.c
│   ├── solver.h
│   ├── domain.c
│   ├── domain.h
│   ├── constraint.c
│   ├── constraint.h
│   ├── proof.c
│   ├── proof.h
│   ├── canon.c
│   ├── canon.h
│   ├── theory.c
│   ├── theory.h
│   ├── midi.c
│   └── midi.h
│
├── tests/
│   ├── test_domains.c
│   ├── test_constraints.c
│   ├── test_solver.c
│   └── test_canon.c
│
├── examples/
│   ├── simple_2voice.json
│   ├── strict_counterpoint.json
│   └── phase_canon.json
│
└── output/
```

---

# 145. Testing Strategy

Each musical rule should have unit tests.

Example:

```text
test_parallel_fifth_detected
test_parallel_fifth_not_detected_contrary_motion
test_parallel_octave_detected
test_range_elimination
test_scale_filter
test_canon_mapping
test_backtrack_restores_domains
test_proof_event_created
```

This matters because music-theory rules can easily contain subtle edge cases.

---

# 146. Property-Based Tests

Useful properties:

- solved compositions never have empty domains
- every collapsed variable has exactly one value
- backtracking restores previous state
- every proof elimination corresponds to an actual constraint
- deterministic mode is reproducible
- canon mapping preserves required transformation

---

# 147. Benchmarking

Measure:

```text
variables
constraints
propagation events
search decisions
backtracks
runtime
peak memory
```

Then evaluate scaling as melody length and voice count increase.

---

# 148. Experimental Questions

Canon Collapse can support actual experiments.

## Experiment A

How does canon delay affect the number of valid solutions?

Measure:

\[
N_{\text{solutions}}(d).
\]

---

## Experiment B

How does voice count affect entropy collapse speed?

Measure:

\[
H(k)
\]

after each solver step for 2, 3, and 4 voices.

---

## Experiment C

Which constraints eliminate the most candidate states?

Use constraint-impact statistics.

---

## Experiment D

Which melody positions have the greatest structural sensitivity?

Compute \(S_i\).

---

## Experiment E

How does temperature affect melodic novelty versus rule cost?

Generate hundreds of samples.

---

# 149. Research-Style Graphs

Possible figures:

- entropy vs solver step
- energy vs musical time
- number of valid states vs canon delay
- backtracks vs voice count
- constraint eliminations by rule
- sensitivity heatmap
- key probability over time
- tension curve
- motif recurrence map

---

# 150. Demo Scenario

A polished demo could proceed like this:

### Step 1

Open the program.

```text
Canon:
3 voices

Delay:
4 beats

Key:
unresolved

Rule set:
Baroque-inspired
```

### Step 2

Click **Generate**.

The score begins partially unresolved.

Notes display candidate domains.

### Step 3

The engine chooses:

```text
Beat 4 = D
```

Several distant beats immediately lose possibilities.

### Step 4

The entropy graph drops.

### Step 5

Beat 12 becomes forced.

Click it.

The proof says:

```text
C rejected:
parallel octave

E rejected:
suspension failure

F rejected:
voice-leading conflict

G remains
```

### Step 6

Press play.

Hear the canon.

### Step 7

Change one note manually.

```text
Beat 4:
D → Eb
```

The piece partially de-collapses and re-collapses.

Harmony changes.

Key interpretation changes.

Several later notes change.

### Step 8

Open the proof graph and show the causal chain.

That is an extremely strong project demonstration.

---

# 151. Possible Names

Primary:

**Canon Collapse**

Alternatives:

- State of Canon
- Collapse Counterpoint
- Constraint Canon
- Resonant State
- Tonal Collapse
- Harmonic State Machine
- Proof of Music
- Counterpoint Engine
- Deterministic Counterpoint
- Symbolic Resonance

**Canon Collapse** is probably the strongest because the phrase connects the musical structure and the state-space metaphor immediately.

---

# 152. One-Sentence Pitch

> **Canon Collapse is a C-based symbolic composition engine that generates self-consistent canons by treating every musical decision as a constraint-propagation event and producing a proof trace explaining why each note survives.**

---

# 153. Short Project Description

Canon Collapse is an interpretable symbolic music-composition engine written in C. Rather than predicting notes using a neural network, it represents a composition as a large mathematical state space of possible pitches, rhythms, harmonies, keys, and canon relationships. Each decision reduces this space and propagates constraints throughout the piece. Because a canon reuses one melody in shifted or transformed voices, a single note may influence many later harmonic contexts. The engine tracks these dependencies, backtracks when contradictions occur, and emits proof traces explaining why each final note was chosen.

---

# 154. Technical Summary

Core topics:

- constraint satisfaction
- graph theory
- combinatorics
- modular arithmetic
- information theory
- optimization
- symbolic AI
- backtracking
- SAT-style reasoning
- music theory
- counterpoint
- tonal harmony
- algorithmic composition
- explainable systems

---

# 155. Strongest Features

If the project becomes too large, prioritize these.

1. **Canon self-constraint**
2. **Constraint propagation**
3. **Forced note collapse**
4. **Proof traces**
5. **Entropy measurement**
6. **Interactive note mutation**
7. **Counterfactual explanations**
8. **Proof graph**
9. **Energy/tension model**
10. **Dynamic scale/key state**

These features are the core identity.

---

# 156. What Makes the Project Distinct

Many music generators answer:

> What note is statistically likely next?

Canon Collapse asks:

> Which notes are mathematically legal, which constraints eliminate the alternatives, and how does one decision change the entire remaining composition?

That difference is fundamental.

It turns generation into reasoning.

---

# 157. Final Conceptual Model

At any moment, the program maintains:

\[
\mathcal{S}
=
(
D_{\text{pitch}},
D_{\text{rhythm}},
D_{\text{harmony}},
D_{\text{key}},
D_{\text{form}},
C,
E,
H,
P
)
\]

where:

- \(D_{\text{pitch}}\): pitch domains
- \(D_{\text{rhythm}}\): rhythm domains
- \(D_{\text{harmony}}\): harmonic domains
- \(D_{\text{key}}\): tonal domains
- \(D_{\text{form}}\): structural domains
- \(C\): active constraints
- \(E\): musical energy
- \(H\): unresolved entropy
- \(P\): proof graph

A decision applies an operator

\[
\mathcal{S}_{t+1}
=
\Phi(\mathcal{S}_t,a_t)
\]

where \(a_t\) is a musical assignment.

The propagation operator \(\Phi\):

1. applies the assignment
2. evaluates affected constraints
3. removes invalid values
4. updates higher-level states
5. records proof events
6. recalculates entropy
7. recalculates energy
8. detects contradictions
9. triggers forced collapses
10. repeats until stable

A stable propagation step satisfies:

\[
\Phi(\mathcal{S})=\mathcal{S}
\]

with respect to deterministic constraint reduction.

Generation ends when every required variable is collapsed.

---

# 158. The Core Thesis

The final conceptual thesis of the project can be stated as:

> **Music can be treated as a structured possibility space rather than a sequence of isolated decisions. In a canon, every note participates in multiple temporal and harmonic contexts, causing local choices to produce nonlocal consequences. By representing these dependencies as constraints, the composition can be generated through iterative state collapse while preserving a complete explanation of the reasoning that produced it.**

That idea is the center of Canon Collapse.

Everything else—entropy, energy, modulation, proof graphs, counterfactuals, temperature, symmetry, motif emergence, and interactive recollapse—extends that central model.

---

# 159. First Concrete Build Target

The project should begin with this exact milestone:

```text
INPUT
-----
Length: 12 notes
Voices: 2
Delay: 4 beats
Scale: C major
Range: C4–C5
Rhythm: quarter notes

RULES
-----
stay in scale
maximum melodic leap = perfect fifth
forbid parallel fifths
forbid parallel octaves
avoid strong-beat seconds
canon must remain valid

OUTPUT
------
1. generated melody
2. second delayed voice
3. MIDI file
4. proof trace
5. entropy log
6. number of backtracks
```

Once this works reliably, the rest of the project can grow around it.

---

# 160. Long-Term Vision

The mature version of Canon Collapse is not just a music generator.

It is a **laboratory for musical possibility spaces**.

A user can:

- define musical laws
- generate music under those laws
- inspect why each note exists
- perturb the composition
- observe nonlocal consequences
- measure uncertainty
- measure tension
- compare rule systems
- study emergent motifs
- visualize causal structure
- analyze existing music
- explore mathematical properties of canons

The system would make composition inspectable in a way black-box generative models usually are not.

The final project is therefore simultaneously:

- a composer
- a solver
- a proof engine
- a visualization system
- a music-theory experiment
- a mathematical playground
- an explainable AI project
- and a systems-programming project.

That combination is the real strength of **Canon Collapse**.
