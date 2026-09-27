#include "domain.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

int main(void) {
    MidiDomain d;
    MidiDomain e;

    /* set and clear bits */
    domain_clear(&d);
    CHECK(domain_count(&d) == 0);
    CHECK(!domain_contains(&d, 60));
    domain_add(&d, 60);
    CHECK(domain_contains(&d, 60));
    CHECK(domain_count(&d) == 1);
    domain_remove(&d, 60);
    CHECK(!domain_contains(&d, 60));
    CHECK(domain_count(&d) == 0);

    /* count */
    domain_clear(&d);
    domain_add(&d, 0);
    domain_add(&d, 64);
    domain_add(&d, 127);
    CHECK(domain_count(&d) == 3);
    domain_add(&d, 64); /* duplicate */
    CHECK(domain_count(&d) == 3);

    /* singleton value */
    domain_clear(&d);
    CHECK(!domain_singleton(&d));
    CHECK(domain_value(&d) == -1);
    domain_add(&d, 72);
    CHECK(domain_singleton(&d));
    CHECK(domain_value(&d) == 72);
    domain_add(&d, 73);
    CHECK(!domain_singleton(&d));
    CHECK(domain_value(&d) == -1);

    /* range fill */
    domain_fill_range(&d, 60, 67);
    CHECK(domain_count(&d) == 8);
    CHECK(domain_contains(&d, 60));
    CHECK(domain_contains(&d, 67));
    CHECK(!domain_contains(&d, 59));
    CHECK(!domain_contains(&d, 68));
    /* clears first, then clips inclusive */
    domain_fill_range(&d, -5, 2);
    CHECK(domain_count(&d) == 3);
    CHECK(domain_contains(&d, 0));
    CHECK(domain_contains(&d, 2));
    CHECK(!domain_contains(&d, 3));
    domain_fill_range(&d, 126, 200);
    CHECK(domain_count(&d) == 2);
    CHECK(domain_contains(&d, 126));
    CHECK(domain_contains(&d, 127));

    /* equality */
    domain_clear(&d);
    domain_clear(&e);
    CHECK(domain_equal(&d, &e));
    domain_add(&d, 40);
    CHECK(!domain_equal(&d, &e));
    domain_add(&e, 40);
    CHECK(domain_equal(&d, &e));
    domain_add(&d, 100);
    domain_add(&e, 101);
    CHECK(!domain_equal(&d, &e));

    /* reject -1 and 128 */
    domain_clear(&d);
    domain_add(&d, -1);
    domain_add(&d, 128);
    CHECK(domain_count(&d) == 0);
    CHECK(!domain_contains(&d, -1));
    CHECK(!domain_contains(&d, 128));
    domain_add(&d, 60);
    domain_remove(&d, -1);
    domain_remove(&d, 128);
    CHECK(domain_contains(&d, 60));
    CHECK(domain_count(&d) == 1);

    /* bit boundary at 63 and 64 */
    domain_clear(&d);
    domain_add(&d, 63);
    domain_add(&d, 64);
    CHECK(domain_contains(&d, 63));
    CHECK(domain_contains(&d, 64));
    CHECK(domain_count(&d) == 2);
    /* 63 is last bit of bits[0]; 64 is first bit of bits[1] */
    CHECK((d.bits[0] & ((uint64_t)1 << 63)) != 0);
    CHECK((d.bits[1] & ((uint64_t)1 << 0)) != 0);
    CHECK((d.bits[0] & ((uint64_t)1 << 0)) == 0);
    domain_remove(&d, 63);
    CHECK(!domain_contains(&d, 63));
    CHECK(domain_contains(&d, 64));
    CHECK(domain_count(&d) == 1);
    CHECK(domain_value(&d) == 64);
    CHECK(domain_next(&d, 0) == 64);
    CHECK(domain_next(&d, 64) == 64);
    CHECK(domain_next(&d, 65) == -1);

    domain_clear(&d);
    domain_add(&d, 3);
    domain_add(&d, 63);
    domain_add(&d, 127);
    CHECK(domain_next(&d, -5) == 3);
    CHECK(domain_next(&d, 4) == 63);
    CHECK(domain_next(&d, 64) == 127);
    CHECK(domain_next(&d, 127) == 127);
    CHECK(domain_next(&d, 128) == -1);
    CHECK(domain_count(&d) == 3);

    printf("ok\n");
    return 0;
}
