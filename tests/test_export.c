#include "canon.h"
#include "export.h"
#include "json.h"
#include "page.h"
#include "run.h"
#include "score.h"
#include "theory.h"
#include "trace.h"
#include "test_util.h"

/* Values for a hand-picked melody: pitches (0 = rest) and holds. */
static void fill_values(const Model *m, int key, const int *pitches, const int *holds,
                        int *values) {
    for (int v = 0; v < m->nvars; v++) values[v] = -1;
    for (int s = 0; s < m->nsections; s++) values[m->key[s]] = key;
    for (int i = 0; i < m->config.length; i++) {
        values[m->pitch[i]] = pitches[i];
        if (m->tie[i] >= 0) values[m->tie[i]] = holds[i] ? TIE_HOLD : TIE_NOTE;
    }
}

static void test_score(void) {
    PieceConfig c = test_config();
    test_set(&c, "rhythm=1");
    test_set(&c, "length=8");
    TestRun *r = test_open(&c);
    static const int pitches[8] = {60, 60, 64, 0, 67, 67, 65, 64};
    static const int holds[8] = {0, 1, 0, 0, 0, 1, 0, 0};
    int values[VAR_MAX];
    fill_values(&r->model, key_id(0, MODE_MAJOR), pitches, holds, values);
    Score s;
    score_build(&s, &r->model, values);
    CHECK(s.span == 12);
    const ScoreVoice *lead = &s.voice[0];
    CHECK(lead->count == 7);
    CHECK(lead->notes[0].start == 0 && lead->notes[0].length == 2 && lead->notes[0].pitch == 60);
    CHECK(lead->notes[1].pitch == 64 && lead->notes[1].length == 1);
    CHECK(lead->notes[2].pitch == SOUND_REST && lead->notes[2].source == -1);
    CHECK(lead->notes[3].pitch == 67 && lead->notes[3].length == 2 && lead->notes[3].source == 4);
    CHECK(lead->notes[6].pitch == SOUND_REST && lead->notes[6].start == 8 &&
          lead->notes[6].length == 4);
    const ScoreVoice *follow = &s.voice[1];
    CHECK(follow->notes[0].pitch == SOUND_REST && follow->notes[0].length == 4);
    CHECK(follow->notes[1].start == 4 && follow->notes[1].length == 2);
    int total = 0;
    for (int k = 0; k < follow->count; k++) total += follow->notes[k].length;
    CHECK(total == s.span);
    test_close(r);

    /* augmentation turns each follower note into a half note */
    c = test_config();
    test_set(&c, "augment=2");
    test_set(&c, "length=4");
    test_set(&c, "delay=2");
    r = test_open(&c);
    static const int four[4] = {60, 64, 67, 72};
    static const int none[4] = {0, 0, 0, 0};
    fill_values(&r->model, key_id(0, MODE_MAJOR), four, none, values);
    score_build(&s, &r->model, values);
    CHECK(s.span == 10);
    CHECK(s.voice[1].notes[1].pitch == 60 && s.voice[1].notes[1].length == 2);
    CHECK(s.voice[1].notes[2].pitch == 64 && s.voice[1].notes[2].start == 4);
    test_close(r);
}

static void test_musicxml(void) {
    test_output_dir();
    PieceConfig c = test_config();
    test_set(&c, "rhythm=1");
    test_set(&c, "max_hold=2");
    test_set(&c, "length=8");
    test_set(&c, "key=F");
    test_set(&c, "modulate_at=8");
    test_set(&c, "key_second=C");
    TestRun *r = test_open(&c);
    /* dotted half at 0, then a half from beat 4 tied over the barline */
    static const int pitches[8] = {65, 65, 65, 70, 70, 69, 67, 65};
    static const int holds[8] = {0, 1, 1, 0, 1, 0, 0, 0};
    int values[VAR_MAX];
    fill_values(&r->model, key_id(5, MODE_MAJOR), pitches, holds, values);
    values[r->model.key[1]] = key_id(0, MODE_MAJOR);
    Score s;
    score_build(&s, &r->model, values);
    CHECK(export_musicxml("output/tests/score.musicxml", &s));
    char *xml = test_slurp("output/tests/score.musicxml", NULL);
    CHECK(strncmp(xml, "<?xml", 5) == 0);
    CHECK(strstr(xml, "<fifths>-1</fifths><mode>major</mode>") != NULL);
    CHECK(strstr(xml, "<fifths>0</fifths>") != NULL); /* the change to C */
    CHECK(strstr(xml, "<duration>3</duration><type>half</type><dot/>") != NULL);
    CHECK(strstr(xml, "<step>B</step><alter>-1</alter>") != NULL);
    CHECK(strstr(xml, "<tie type=\"start\"/>") != NULL);
    CHECK(strstr(xml, "<tie type=\"stop\"/>") != NULL);
    CHECK(strstr(xml, "<measure number=\"3\">") != NULL);
    CHECK(strstr(xml, "<measure number=\"4\">") == NULL);
    const char *p2 = strstr(xml, "<part id=\"P2\">");
    CHECK(p2 != NULL);
    CHECK(strstr(p2, "<rest/><duration>4</duration><type>whole</type>") != NULL);
    free(xml);

    /* a dorian piece names its mode */
    Score d = s;
    d.key[0] = key_id(2, MODE_DORIAN);
    d.nsections = 1;
    CHECK(export_musicxml("output/tests/dorian.musicxml", &d));
    char *dxml = test_slurp("output/tests/dorian.musicxml", NULL);
    CHECK(strstr(dxml, "<fifths>0</fifths><mode>dorian</mode>") != NULL);
    free(dxml);

    CHECK(export_contour("output/tests/contour.svg", &s));
    char *svg = test_slurp("output/tests/contour.svg", NULL);
    CHECK(strncmp(svg, "<svg", 4) == 0);
    CHECK(strstr(svg, "polyline") != NULL);
    free(svg);

    long size = 0;
    long table_size = 0;
    CHECK(export_wav("output/tests/voices.wav", &s, 0));
    CHECK(export_wav("output/tests/table.wav", &s, 1));
    char *wav = test_slurp("output/tests/voices.wav", &size);
    char *table = test_slurp("output/tests/table.wav", &table_size);
    long per_step = 44100L * 60 / c.tempo;
    CHECK(size == 44 + 2 * per_step * s.span);
    CHECK(table_size == size);
    CHECK(memcmp(wav, "RIFF", 4) == 0 && memcmp(wav + 8, "WAVE", 4) == 0);
    CHECK(memcmp(wav + 36, "data", 4) == 0);
    CHECK(memcmp(wav + 44, table + 44, (size_t)(size - 44)) != 0);
    free(wav);
    free(table);
    test_close(r);
}

static void test_documents(void) {
    test_output_dir();
    Run *run = malloc(sizeof(Run));
    CHECK(run != NULL);
    char err[200];
    PieceConfig c = test_config();
    test_set(&c, "rhythm=1");
    test_set(&c, "harmony=1");
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(run->status == SOLVE_SAT);

    CHECK(trace_save_json("output/tests/piece.json", run));
    char *text = test_slurp("output/tests/piece.json", NULL);
    JsonValue doc;
    char why[128];
    CHECK(json_parse(text, &doc, why, sizeof(why)));
    free(text);
    const JsonValue *events = json_get(&doc, "events");
    CHECK(events != NULL && events->type == JSON_ARRAY);
    CHECK(events->count == run->state.proof.event_count);
    int linked = 0;
    for (int i = 0; i < events->count; i++) {
        const JsonValue *parents = json_get(&events->items[i], "parents");
        CHECK(parents != NULL && parents->type == JSON_ARRAY);
        for (int p = 0; p < parents->count; p++) {
            const JsonValue *id = json_get(&parents->items[p], "event");
            CHECK(id != NULL && id->number < i);
            if (id->number >= 0) linked++;
        }
    }
    CHECK(linked > 0);
    const JsonValue *vars = json_get(&doc, "variables");
    CHECK(vars != NULL && vars->count == run->model.nvars);
    int chosen = 0;
    for (int v = 0; v < vars->count; v++) {
        const JsonValue *status = json_get(&vars->items[v], "status");
        CHECK(status != NULL && status->type == JSON_STRING);
        chosen += strcmp(status->string, "chosen") == 0;
    }
    CHECK(chosen > 0);
    /* each variable carries its initial domain, sorted, for the collapse replay */
    for (int v = 0; v < vars->count; v++) {
        const JsonValue *initial = json_get(&vars->items[v], "initial");
        CHECK(initial != NULL && initial->type == JSON_ARRAY);
        CHECK(initial->count == domain_count(&run->model.initial[v]));
        for (int k = 0; k < initial->count; k++) {
            CHECK(domain_contains(&run->model.initial[v], (int)initial->items[k].number));
            if (k > 0) CHECK(initial->items[k].number > initial->items[k - 1].number);
        }
    }
    CHECK(json_get(&doc, "counterfactual") == NULL);
    const JsonValue *score = json_get(&doc, "score");
    CHECK(score != NULL && score->count == run->model.voices);
    CHECK(json_get(&doc, "chords")->count == run->model.nbars);
    CHECK(json_get(json_get(&doc, "energy"), "total")->number == run->energy);
    json_free(&doc);

    CHECK(page_write("output/tests/score.html", run));
    char *html = test_slurp("output/tests/score.html", NULL);
    CHECK(strncmp(html, "<!DOCTYPE html>", 15) == 0);
    CHECK(strstr(html, "/*PIECE_DATA*/") == NULL);
    CHECK(strstr(html, "\"events\":[") != NULL);
    CHECK(strstr(html, "\"initial\":[") != NULL);
    CHECK(strstr(html, "id=\"cx-grid\"") != NULL);
    /* a template checked out with CRLF line endings must not double them */
    CHECK(strstr(html, "\r\r") == NULL);
    free(html);

    CHECK(trace_write_text("output/tests/proof.txt", run));
    text = test_slurp("output/tests/proof.txt", NULL);
    CHECK(strstr(text, "remove x0 C#4 scale") != NULL);
    CHECK(strstr(text, "decide ") != NULL);
    CHECK(strstr(text, "entropy ") != NULL);
    free(text);
    CHECK(trace_write_dag("output/tests/proof.dag", run));
    text = test_slurp("output/tests/proof.dag", NULL);
    CHECK(strstr(text, "parent event") != NULL);
    CHECK(strstr(text, "parent assign key = C major") != NULL);
    free(text);
    run_free(run);

    /* a given note adds the piece solved without it */
    test_set(&c, "lock=1");
    test_set(&c, "lock_index=3");
    test_set(&c, "lock_pitch=67");
    CHECK(run_piece(run, &c, err, sizeof(err)));
    CHECK(run->status == SOLVE_SAT && run->counterfactual);
    CHECK(trace_save_json("output/tests/locked.json", run));
    text = test_slurp("output/tests/locked.json", NULL);
    CHECK(json_parse(text, &doc, why, sizeof(why)));
    free(text);
    const JsonValue *cf = json_get(&doc, "counterfactual");
    CHECK(cf != NULL && cf->type == JSON_OBJECT);
    CHECK(strcmp(json_get(cf, "unlockedStatus")->string,
                 solve_status_name(run->unlocked_status)) == 0);
    const JsonValue *pitch = json_get(cf, "unlockedPitch");
    const JsonValue *changed = json_get(cf, "changed");
    CHECK(pitch != NULL && changed != NULL && changed->type == JSON_ARRAY);
    if (run->unlocked_status == SOLVE_SAT) {
        CHECK(pitch->count == c.length);
        CHECK(json_get(cf, "unlockedLabel")->count == c.length);
        int differ = 0;
        for (int i = 0; i < c.length; i++)
            differ += run->values[run->model.pitch[i]] != run->unlocked_pitch[i];
        CHECK(changed->count == differ);
        for (int k = 0; k < changed->count; k++) {
            int i = (int)changed->items[k].number;
            CHECK(run->values[run->model.pitch[i]] != (int)pitch->items[i].number);
        }
    }
    json_free(&doc);
    run_free(run);
    free(run);
}

int main(void) {
    test_score();
    test_musicxml();
    test_documents();
    printf("ok\n");
    return 0;
}
