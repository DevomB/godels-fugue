# End-to-end tests of the command-line program.
#   cmake -DEXE=path/to/canon-collapse -DSRC=source/dir -DOUT=scratch/dir -P cli.cmake
cmake_minimum_required(VERSION 3.16)

set(failures 0)

function(fail message)
  message(SEND_ERROR "${message}")
  math(EXPR n "${failures} + 1")
  set(failures ${n} PARENT_SCOPE)
endfunction()

# run(NAME EXPECT_RC [MATCH regex...] [ERROR regex...] ARGS args...)
function(run name expect)
  cmake_parse_arguments(R "" "" "MATCH;ERROR;ARGS" ${ARGN})
  set(dir "${OUT}/${name}")
  file(REMOVE_RECURSE "${dir}")
  execute_process(
    COMMAND "${EXE}" --out "${dir}/canon.mid" --proof "${dir}/proof.txt"
            --entropy "${dir}/entropy.txt" ${R_ARGS}
    WORKING_DIRECTORY "${SRC}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  if(NOT rc EQUAL expect)
    fail("${name}: exit ${rc}, expected ${expect}\n${out}\n${err}")
    set(failures ${failures} PARENT_SCOPE)
    return()
  endif()
  foreach(pattern IN LISTS R_MATCH)
    if(NOT out MATCHES "${pattern}")
      fail("${name}: stdout lacks ${pattern}\n${out}")
    endif()
  endforeach()
  foreach(pattern IN LISTS R_ERROR)
    if(NOT err MATCHES "${pattern}")
      fail("${name}: stderr lacks ${pattern}\n${err}")
    endif()
  endforeach()
  set(failures ${failures} PARENT_SCOPE)
  set(last_dir "${dir}" PARENT_SCOPE)
  set(last_out "${out}" PARENT_SCOPE)
endfunction()

function(expect_file path magic)
  if(NOT EXISTS "${path}")
    fail("missing ${path}")
  else()
    file(SIZE "${path}" size)
    if(size EQUAL 0)
      fail("empty ${path}")
    endif()
    if(NOT magic STREQUAL "")
      file(READ "${path}" head LIMIT 15)
      string(FIND "${head}" "${magic}" at)
      if(NOT at EQUAL 0)
        fail("${path} does not start with ${magic}")
      endif()
    endif()
  endif()
  set(failures ${failures} PARENT_SCOPE)
endfunction()

# A default run writes every output file.
run(default 0 MATCH "key: C major" "melody:" "backtracks:" "entropy:" "energy:")
foreach(pair IN ITEMS "canon.mid|MThd" "score.musicxml|<?xml" "score.ly|\\version"
                      "score.abc|X:" "contour.svg|<svg"
                      "voices.wav|RIFF" "score.html|<!DOCTYPE html>" "proof.json|{"
                      "report.txt|Canon Collapse" "explain.txt|" "proof.txt|" "proof.dag|"
                      "entropy.txt|")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 name)
  list(LENGTH parts n)
  set(magic "")
  if(n GREATER 1)
    list(GET parts 1 magic)
  endif()
  expect_file("${last_dir}/${name}" "${magic}")
endforeach()

# Every example: the unsatisfiable ones exit 1 and name a core.
set(unsat_examples lock unsat)
file(GLOB examples "${SRC}/examples/*.txt" "${SRC}/examples/*.json")
list(LENGTH examples count)
if(count LESS 20)
  fail("expected at least 20 examples, found ${count}")
endif()
foreach(path IN LISTS examples)
  get_filename_component(name "${path}" NAME_WE)
  get_filename_component(ext "${path}" EXT)
  if(name IN_LIST unsat_examples)
    run("example_${name}" 1 ERROR "core:" ARGS --config "${path}")
  else()
    run("example_${name}${ext}" 0 MATCH "melody:" ARGS --config "${path}")
  endif()
endforeach()

foreach(preset IN ITEMS renaissance baroque classical minimalist experimental)
  run("preset_${preset}" 0 MATCH "melody:" ARGS --preset ${preset})
endforeach()

run(explain 0 MATCH "x3 = " "candidates by cost|removed" ARGS --explain 3)
run(explain_key 0 MATCH "key = C major" ARGS --explain key)
run(explain_bad 1 ERROR "no variable named" ARGS --explain nope)
# the README's example: x11 narrowed x0 before x0 was chosen, yet x0 and x4 alone force x8
run(explain_minimal 0 MATCH "depends on: x0 = C4, x4 = G4\r?\n"
    ARGS --config "${SRC}/examples/cyclic.txt" --set time_limit=0 --explain x8)
run(lock_cli 0 MATCH "counterfactual: lock x0 = E4" ARGS --lock 0 64)
run(given 0 MATCH "melody: 64 67 69 67 .* 60" "counterfactual: given x0 = E4, x1 = G4" "changed:"
    ARGS --config "${SRC}/examples/given.txt")
run(given_bad 1 ERROR "invalid melody" ARGS --set "melody=60,?,rest")
run(leading_tone 1 ERROR "core: leading tone" "rises to the tonic at note 1"
    ARGS --set leading_tone=1 --set "melody=71,67")
run(double_leap 1 ERROR "core: double leap" "two leaps over 4 semitones"
    ARGS --set double_leaps=0 --set max_leap=12 --set range_high=84 --set "melody=60,67,74")
run(contrary 0 MATCH "melody:" ARGS --set voices=3 --set w_contrary=4)
run(check_bad 1 MATCH "violation: scale: notes stay in the key \\(x1 = C#4"
    ARGS --set "melody=60,61" --check)
if(EXISTS "${last_dir}")
  fail("check_bad: --check wrote output files")
endif()
run(check_given 0 MATCH "violations: none" ARGS --config "${SRC}/examples/given.txt" --check)
run(check_fail 1 ERROR "violation: consonance: voices 1,2 must be consonant at step 4.*core:"
    ARGS --set "melody=60,?,?,?,62")
file(READ "${last_dir}/report.txt" report)
if(NOT report MATCHES "violations:[\r\n]+  consonance: ")
  fail("check_fail: report.txt lacks the violations")
endif()
file(READ "${last_dir}/proof.json" json)
if(NOT json MATCHES "\"violations\":\\[{\"rule\":\"consonance\"")
  fail("check_fail: proof.json lacks the violations")
endif()
run(mirror_note 0 MATCH "melody:" ERROR "mirroring the melody around axis 66 keeps 3 of 7 notes"
    ARGS --set mirror=1)
run(mirror_range 1 ERROR "invalid mirror_axis" ARGS --set mirror=1 --set mirror_axis=80)
run(set 0 MATCH "voice 3:" ARGS --set voices=3)
run(sat 0 MATCH "melody:" ARGS --sat)
run(sat_unsat 1 MATCH "unsat" ARGS --config "${SRC}/examples/unsat.txt" --sat)
run(count 0 MATCH "count: 160 \\(exact\\)" ARGS --config "${SRC}/examples/sensitivity.txt" --count 1000)
run(count_max 0 MATCH "count: at least 5 \\(stopped at 5\\)" ARGS --count 5)
run(count_limit 3 MATCH "stopped at the search limit" ARGS --count 100000 --max-nodes 3)
run(count_unsat 0 MATCH "count: 0 \\(exact\\)" ARGS --config "${SRC}/examples/unsat.txt" --count 10)
run(count_bad 1 ERROR "positive number" ARGS --count 0)
run(count_sat 1 ERROR "fixed delay" ARGS --count 5 --sat)
run(sensitivity 0 MATCH "x0 +frozen +1/1: E4" "x4 +bifurcation point" "x11 +frozen +1/1: C4"
    ARGS --config "${SRC}/examples/given.txt" --sensitivity)
run(sensitivity_unsat 0 MATCH "x0 +none +0/0" ARGS --config "${SRC}/examples/unsat.txt" --sensitivity)
if(EXISTS "${OUT}/count/canon.mid")
  fail("--count wrote a score")
endif()
run(limit 3 ERROR "search limit reached" ARGS --max-nodes 2)
run(corpus 0 MATCH "weights:" ARGS --corpus "${SRC}/corpus" --apply-weights)
# A piece's MIDI file gives back its melody, rests and all (the character
# after the line ends it), and a corpus can be MIDI files.
run(midi_piece 0 MATCH "melody:" ARGS)
string(REGEX MATCH "melody:( [0-9]+| rest)+" melody_line "${last_out}")
run(melody_midi 0 MATCH "${melody_line}[^ 0-9a-z]" "counterfactual: given"
    ARGS --melody-midi "${last_dir}/canon.mid")
run(midi_rests 0 MATCH "melody:.* rest" ARGS --set rhythm=1 --set rest_at=3)
string(REGEX MATCH "melody:( [0-9]+| rest)+" melody_line "${last_out}")
run(melody_midi_rests 0 MATCH "${melody_line}[^ 0-9a-z]"
    ARGS --set rhythm=1 --melody-midi "${OUT}/midi_rests/canon.mid")
run(melody_midi_rhythm 1 ERROR "rests at step [0-9]+, and rests need rhythm: add --set rhythm=1"
    ARGS --melody-midi "${OUT}/midi_rests/canon.mid")
run(melody_midi_text 1 ERROR "not a Standard MIDI file"
    ARGS --melody-midi "${SRC}/examples/given.txt")
run(melody_midi_missing 1 ERROR "cannot read MIDI file" ARGS --melody-midi "${OUT}/none.mid")
file(REMOVE_RECURSE "${OUT}/midi_corpus")
file(MAKE_DIRECTORY "${OUT}/midi_corpus")
file(COPY "${OUT}/midi_rests/canon.mid" DESTINATION "${OUT}/midi_corpus")
run(corpus_midi 0 MATCH "weights:( [0-9]+)+" ARGS --corpus "${OUT}/midi_corpus")
run(delay_search 0 MATCH "delay: [0-9]+" ARGS --set delay_search=1 --set delay_max=5)
run(diatonic_cli 0 MATCH "voice 2: rest rest rest rest 64 65 67 69 rest" "voice 3: [^
]* 67 69 71 72
"
    ARGS --set length=4 --set "melody=60,62,64,65" --set cadence=0 --set voices=3
         --set diatonic=1 --set transpose=2 --set transpose_2=4 --set range_high=74)
run(voice_transpose_cli 0 MATCH "voice 2: rest rest rest rest 67 69 71 72 rest"
         "voice 3: [^
]* 60 62 64 65
"
    ARGS --set length=4 --set "melody=60,62,64,65" --set cadence=0 --set voices=3
         --set transpose=0 --set transpose_1=7 --set range_high=74)
run(diatonic_search 1 ERROR "invalid diatonic: needs a fixed key" ARGS --set diatonic=1 --set key=search)
run(diatonic_modulate 1 ERROR "invalid diatonic: cannot be used with modulate_at"
    ARGS --set diatonic=1 --set modulate_at=8)
run(bad_transpose_1 1 ERROR "transpose_1" ARGS --set transpose_1=30)
run(help 0 MATCH "usage: canon-collapse" ARGS --help)
run(version 0 MATCH "canon-collapse [0-9]" ARGS --version)
run(presets 0 MATCH "baroque" ARGS --list-presets)
run(bad_arg 1 ERROR "unknown argument" ARGS --frobnicate)
run(missing 1 ERROR "missing value for --config" ARGS --config)
run(bad_set 1 ERROR "unknown config key: tempi" ARGS --set tempi=3)
run(bad_value 1 ERROR "voices" ARGS --set voices=9)
run(bad_preset 1 ERROR "unknown preset" ARGS --preset romantic)
run(no_config 1 ERROR "cannot read config" ARGS --config "${SRC}/examples/none.txt")
run(dir_config 1 ERROR "cannot read config" ARGS --config "${SRC}/examples")
run(bad_corpus 1 ERROR "cannot read corpus" ARGS --corpus "${SRC}/nothing-here")

run(collide 1 ERROR "both be written" ARGS --proof "${OUT}/collide/score.html")
run(collide_dag 1 ERROR "both be written" ARGS --entropy "${OUT}/collide_dag/proof.dag")
run(lock_limit 3 MATCH "inconclusive" ERROR "search limit"
    ARGS --set optimize=0 --set length=16 --set voices=3 --set delay=2 --set range_low=55
         --set range_high=74 --set consonance=all --set harmony=1 --set rhythm=1
         --set backjump=0 --max-nodes 11 --lock 4 55)

# A failed run clears the score files an earlier run left in the same place.
set(dir "${OUT}/stale")
file(REMOVE_RECURSE "${dir}")
execute_process(COMMAND "${EXE}" --out "${dir}/canon.mid" --proof "${dir}/proof.txt"
                        --entropy "${dir}/entropy.txt" WORKING_DIRECTORY "${SRC}"
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND "${EXE}" --out "${dir}/canon.mid" --proof "${dir}/proof.txt"
                        --entropy "${dir}/entropy.txt" --config "${SRC}/examples/unsat.txt"
                WORKING_DIRECTORY "${SRC}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
foreach(name IN ITEMS canon.mid score.musicxml score.ly score.abc contour.svg voices.wav)
  if(EXISTS "${dir}/${name}")
    fail("stale ${name} left beside a failed run")
  endif()
endforeach()
expect_file("${dir}/score.html" "<!DOCTYPE html>")

file(WRITE "${OUT}/bad_key.txt" "length 12\nstrong_chord 1\n")
run(bad_key 1 ERROR "unknown config key: strong_chord" ARGS --config "${OUT}/bad_key.txt")

# The config reference in the docs matches the program.
run(reference 0 ARGS --list-config --markdown)
file(READ "${SRC}/docs/config.md" doc)
string(REPLACE "\r" "" doc "${doc}")
string(REPLACE "\r" "" reference "${last_out}")
string(FIND "${doc}" "${reference}" at)
if(at EQUAL -1)
  fail("docs/config.md is out of date: regenerate it with --list-config --markdown")
endif()

if(failures GREATER 0)
  message(FATAL_ERROR "${failures} CLI checks failed")
endif()
