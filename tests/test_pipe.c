#include "../src/pipe.c"

static int fails;

static void check(const char *seg, const char *in, const char *want)
{
    filter f;
    if (filter_parse(seg, &f) != 0) {
        printf("FAIL parse '%s'\n", seg);
        fails++;
        return;
    }
    char *got = filter_apply(&f, in);
    if (strcmp(got, want) != 0) {
        printf("FAIL '%s': got [%s] want [%s]\n", seg, got, want);
        fails++;
    }
    free(got);
    filter_free(&f);
}

static void check_rejected(const char *seg)
{
    filter f;
    printf("(expected error) ");
    if (filter_parse(seg, &f) == 0) {
        printf("FAIL '%s' should be rejected\n", seg);
        fails++;
        filter_free(&f);
    }
}

int main(void)
{
    const char *in = "en0 up\nlo0 up\nen1 down\n";

    check("include en", in, "en0 up\nen1 down\n");
    check(" inc  en ", in, "en0 up\nen1 down\n");
    check("exclude down", in, "en0 up\nlo0 up\n");
    check("begin lo", in, "lo0 up\nen1 down\n");
    check("begin nomatch", in, "");
    check("count", in, "Number of lines in output: 3\n");
    check("grep -i EN0", in, "en0 up\n");
    check("grep -v up", in, "en1 down\n");
    check("grep -iv EN", in, "lo0 up\n");
    check("include \"en0|lo0\"", in, "en0 up\nlo0 up\n");
    check("include b", "a\nb", "b\n");
    check("count", "", "Number of lines in output: 0\n");
    check("include x", "", "");

    check_rejected("");
    check_rejected("bogus x");
    check_rejected("include");
    check_rejected("include [");
    check_rejected("count extra");
    check_rejected("grep -z x");

    char **segs;
    int n = split_segments("show x | include \"a|b\" | count", &segs);
    if (n != 3 || strcmp(segs[1], " include \"a|b\" ") != 0) {
        printf("FAIL split_segments (n=%d)\n", n);
        fails++;
    }
    free_segments(segs, n);

    if (fails == 0)
        printf("PASS test_pipe\n");
    return fails != 0;
}
