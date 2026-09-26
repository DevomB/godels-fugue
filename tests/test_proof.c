#include "domain.h"
#include "proof.h"

#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static int file_contains(const char *path, const char *needle) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 0) {
        fclose(fp);
        return 0;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    size_t n = fread(buf, 1, (size_t)len, fp);
    buf[n] = '\0';
    fclose(fp);
    int found = strstr(buf, needle) != NULL;
    free(buf);
    return found;
}

int main(void) {
    MidiDomain domains[12];
    for (int i = 0; i < 12; i++) {
        domain_clear(&domains[i]);
        domain_fill_range(&domains[i], 60, 67); /* 8 pitches */
    }
    CHECK(entropy_bits(domains, 12) == 36.0);

    for (int i = 0; i < 12; i++) {
        domain_clear(&domains[i]);
        domain_add(&domains[i], 60);
    }
    CHECK(entropy_bits(domains, 12) == 0.0);

    ProofLog log;
    proof_init(&log);

    CHECK(proof_append_removal(&log, 0, 61, 1, "keep_me"));
    CHECK(proof_append_entropy(&log, 36.0));
    CHECK(proof_append_removal(&log, 1, 62, 2, "drop_me"));
    CHECK(proof_append_entropy(&log, 30.0));
    CHECK(log.event_count == 2);
    CHECK(log.sample_count == 2);

    ProofMark mark = {1, 1};
    proof_truncate(&log, mark);
    CHECK(log.event_count == 1);
    CHECK(log.sample_count == 1);
    CHECK(strcmp(log.events[0].message, "keep_me") == 0);
    CHECK(log.samples[0].bits == 36.0);

    CHECK(proof_append_removal(&log, 2, 63, 3, "after_truncate"));
    CHECK(log.event_count == 2);

    _mkdir("output");
    {
        ProofLog fresh;
        MidiDomain domains[2];
        int related = 0;
        proof_init(&fresh);
        domain_clear(&domains[0]);
        domain_add(&domains[0], 60);
        domain_clear(&domains[1]);
        domain_add(&domains[1], 67);
        domain_add(&domains[1], 72);
        CHECK(proof_append_removal_deps(&fresh, 1, 72, 3, "melodic leap", &related, 1,
                                        domains));
        CHECK(fresh.events[0].parent_count == 1);
        CHECK(fresh.events[0].parent_events[0] == -1);
        CHECK(fresh.events[0].parent_vars[0] == 0);
        CHECK(fresh.events[0].parent_pitches[0] == 60);
        CHECK(proof_write_dag(&fresh, "output/proof_parent.dag"));
        CHECK(file_contains("output/proof_parent.dag", "parent assign 0=60"));
        proof_free(&fresh);
    }

    CHECK(proof_write(&log, "output/proof_test.txt"));
    CHECK(file_contains("output/proof_test.txt", "keep_me"));
    CHECK(file_contains("output/proof_test.txt", "36.000000"));
    CHECK(!file_contains("output/proof_test.txt", "drop_me"));
    CHECK(proof_write_dag(&log, "output/proof_test.dag"));

    proof_free(&log);
    printf("ok\n");
    return 0;
}
