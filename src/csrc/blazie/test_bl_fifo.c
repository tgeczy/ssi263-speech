/* test_bl_fifo.c -- the Braille Lite board's serial input queue (bl_board.c), on a real unit made from the firmware:
 * the bytes the host queues reach the unit in the order they were queued, however many wait (issue #36).
 *
 * The queue was a 64 KB ring with no check against what the unit had read: a single utterance over 64 KB (a SAPI
 * program handing a whole document to one Speak, spd-say with a file) overwrote its own start, and the unit spoke its
 * end, its middle, its end again, and never its beginning.  This test includes bl_board.c to call the board's own
 * read of its serial input, asci_rx(), as the unit's CPU does, after queueing (nothing run):
 *   sizes     1,000 / 65,535 / 65,536 / 70,000 / 300,000 bytes queued at once, then all read back: the same bytes, in
 *             order, and then nothing more
 *   in parts  300,000 bytes queued in 1,000-byte parts with half of each read between: the same
 *   drop      bl_drop() empties the queue and counts its Ctrl-Fs, whatever its size
 *
 *   test_bl_fifo <firmware .bns>        exit 1 on any failure
 */
#include "bl_board.c"

static int failures;

static void report(const char *name, int ok, const char *detail)
{
    printf("%-4s %-9s %s\n", ok ? "ok" : "FAIL", name, detail);
    if (!ok)
        failures++;
}

static unsigned char pattern(long i)
{
    unsigned char b = (unsigned char)((i * 7 + i / 251 + i / 65536 * 13) & 0xFF);
    return b == 0x06 ? 0x07 : b;                  /* no Ctrl-F but where the drop test puts them */
}

/* what the unit reads now, up to n bytes; -1 where it read something else than queued, else the count read */
static long read_back(bl_unit *u, long from, long n, long *bad_at)
{
    long k;
    for (k = 0; k < n; k++) {
        int b = asci_rx(u, 0);
        if (b < 0)
            return k;
        if (b != pattern(from + k)) {
            *bad_at = from + k;
            return -1;
        }
    }
    return k;
}

int main(int argc, char **argv)
{
    static const long SIZES[] = {1000, 65535, 65536, 70000, 300000};
    char err[256], d[200];
    unsigned char *buf;
    bl_unit *u;
    long i, k, got, bad_at;
    int s, ok;
    if (argc < 2) {
        fprintf(stderr, "usage: test_bl_fifo <firmware .bns>\n");
        return 2;
    }
    u = bl_create(argv[1], NULL, 5.0, NULL, NULL, 0, err, (int)sizeof err);
    if (!u) {
        printf("FAIL the unit: %s\n", err);
        return 1;
    }
    u->live_on = 1;                               /* the board's live mode, as bl_live sets it */
    u->urgent = -1;
    u->host_xoff = 0;
    buf = (unsigned char *)malloc(300000);
    for (i = 0; i < 300000; i++)
        buf[i] = pattern(i);

    ok = 1;
    d[0] = 0;
    for (s = 0; s < (int)(sizeof SIZES / sizeof SIZES[0]) && ok; s++) {
        bad_at = -1;
        bl_drop(u);                               /* each size on an empty queue */
        bl_queue(u, buf, (int)SIZES[s]);
        got = read_back(u, 0, SIZES[s] + 1, &bad_at);
        ok = got == SIZES[s];
        snprintf(d, sizeof d, "%ld bytes queued: %s", SIZES[s], got == SIZES[s] ? "all read back in order" :
                 got < 0 ? "a different byte read" : "fewer read back");
        if (bad_at >= 0)
            snprintf(d + strlen(d), sizeof d - strlen(d), " at byte %ld", bad_at);
    }
    report("sizes", ok, ok ? "1,000 to 300,000 bytes queued at once: each read back whole, in order" : d);

    bl_drop(u);
    ok = 1;
    got = 0;
    bad_at = -1;
    for (i = 0; i < 300000 && ok; i += 1000) {
        bl_queue(u, buf + i, 1000);
        k = read_back(u, got, 500, &bad_at);
        ok = k == 500;
        got += k > 0 ? k : 0;
    }
    if (ok) {
        k = read_back(u, got, 300000 - got + 1, &bad_at);
        ok = k == 300000 - got;
        got += k > 0 ? k : 0;
    }
    snprintf(d, sizeof d, "300,000 bytes in 1,000-byte parts, half of each read between: %ld read back%s", got,
             bad_at >= 0 ? " before a different byte" : "");
    report("in parts", ok && got == 300000, d);

    bl_drop(u);
    buf[10] = buf[99999] = buf[150000] = 0x06;
    bl_queue(u, buf, 150001);
    k = bl_drop(u);
    got = asci_rx(u, 0);
    snprintf(d, sizeof d, "150,001 bytes with 3 Ctrl-Fs dropped: %ld counted, then %s", k, got < 0 ? "nothing to read" :
             "a byte still read");
    report("drop", k == 3 && got < 0, d);

    free(buf);
    bl_destroy(u);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
