#ifndef TRACE_H
#define TRACE_H

#include "run.h"

#include <stdbool.h>
#include <stdio.h>

/* proof.txt: every event in order, with entropy after each fixpoint. */
bool trace_write_text(const char *path, const Run *run);
/* proof.dag: each event with the events and assignments it rests on. */
bool trace_write_dag(const char *path, const Run *run);
/* entropy.txt: remaining entropy in bits after each fixpoint. */
bool trace_write_entropy(const char *path, const Run *run);
/* The piece as one JSON document: config, score, every variable's
 * explanation, the proof events with their parents, and statistics. */
void trace_write_json(FILE *f, const Run *run);
/* The members "players" (each voice's instrument, range, transposition and
 * entry), "form" (the phrase plan and the climax) and "keepsNotes" (the keys
 * that change only how the piece is played), as proof.json and --resolve
 * write them. */
void trace_write_plan(FILE *f, const Model *m);
bool trace_save_json(const char *path, const Run *run);

const char *solve_status_name(SolveStatus status);

#endif
