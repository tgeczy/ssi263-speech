/* marks.h -- index marks, as the voices and the voice table (voices.h) pass them.
 *
 * A screen reader marks places in the text it sends -- NVDA's IndexCommand, a SAPI bookmark, an SSML mark, Orca's
 * mark before every word -- and must be told when speech reaches each: that is how its cursor follows the speech and
 * how reading knows where it stopped.  Several of these units have index marks of their own in their firmware, made
 * for exactly that (Aicom's manual: "to track exactly which word in a given text Accent has spoken to").  A voice that
 * uses its unit's marks reports each one where the unit says it is: exact.  A unit without them, or a voice that does
 * not use them yet, has its marks' places estimated (voices.c), and says so.
 *
 * Rules every voice keeps:
 *   - a text with no mark is spoken exactly as before: nothing is sent or asked for marks' sake;
 *   - a mark never changes which words are spoken: the text is prepared whole, as without marks, and a mark is put
 *     at the START of a word.  One that stands inside a word, or inside something the preparation rewrites as a
 *     whole (a number, an amount of money), moves to the start of it;
 *   - marks that fall on the same word are one mark to the unit, and are reported together, in their order;
 *   - after a cancel, no mark of the cancelled text is reported.
 * MIT.
 */
#ifndef SSI263_MARKS_H
#define SSI263_MARKS_H

typedef struct {
    int at;                        /* its place: a byte offset into the UTF-8 text, 0 to the text's length; a caller
                                      gives them in rising order */
    int id;                        /* the caller's own number for it, given back when speech reaches it */
} ssv_mark;

typedef struct {
    int id;
    int sample;                    /* in the block just rendered: speech reaches the mark at this sample, 0 to the
                                      block's count (the count: after its last sample) */
    int exact;                     /* 1: where the unit's own index mark says; 0: estimated */
} ssv_reach;

#endif
