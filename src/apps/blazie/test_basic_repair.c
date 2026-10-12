/* A small BNS BASIC program with bytes older IMG imports damaged: line number
   10, an eleven-byte record's length, an INPUT token, and an IF length. */
#include <stdio.h>
#include <string.h>
#include "../../csrc/blazie/bl_files_xfer.h"

int main(void)
{
    static const unsigned char good[] = {
        0, 10, 0, 10, 'p', 'r', 'i', 'n', 't', ' ', '1',
        0, 20, 0, 8, 10, 1, 2, 'x', '$',
        0, 30, 0, 16, 3, 10, 'w', 'a', 'v', 'e', ' ', '<', '=', ' ', '1', '0', 4,
        0, 40, 0, 7, 'e', 'n', 'd', 0
    };
    unsigned char damaged[sizeof good];
    memcpy(damaged, good, sizeof good);
    damaged[1] = damaged[3] = damaged[15] = damaged[25] = 13;
    if (blx_repair_basic(damaged, sizeof damaged) != 4 || memcmp(damaged, good, sizeof good)) {
        fputs("BNS BASIC IMG repair failed\n", stderr);
        return 1;
    }
    if (blx_repair_basic(damaged, sizeof damaged) != 0 || memcmp(damaged, good, sizeof good)) {
        fputs("BNS BASIC repair changed a clean program\n", stderr);
        return 1;
    }
    memcpy(damaged, good, sizeof good);
    damaged[25] = 13; /* Partly repaired programs already contain LF bytes. */
    if (blx_repair_basic(damaged, sizeof damaged) != 1 || memcmp(damaged, good, sizeof good)) {
        fputs("BNS BASIC partial repair failed\n", stderr);
        return 1;
    }
    {
        static const unsigned char fields[] = {
            0, 1, 0, 17, 24, 10, 'c','o','u','n','t','e','r','i','n','g', 1,'1',
            0, 2, 0, 16, 1,1,10, '"','C','o','r','r','e','c','t','.','"',
            0, 3, 0, 20, 26,2,10, '"','Y','o','u',' ','R','u','l','e','"', 3,'"','a','"',
            0, 4, 0, 29, 11,10, 'c','o','u','n','t','e','r','i','n','g', 1,'1',12,10,
                'l','e','n','(','w','o','r','d','$',')',
            0, 5, 0, 18, 30,2,10, 'l','u','c','k','(','u','s','i','t',')', 1,'1',
            0, 6, 0, 19, 1,1,13, '"','P','l','e','a','s','e',' ','w','a','i','t','"'
        };
        static const unsigned positions[] = {5, 24, 41, 61, 75, 92};
        unsigned char copy[sizeof fields];
        unsigned i;
        memcpy(copy, fields, sizeof copy);
        for (i = 0; i < sizeof positions / sizeof positions[0]; i++) copy[positions[i]] = 13;
        if (blx_repair_basic(copy, sizeof copy) != 6 || memcmp(copy, fields, sizeof fields) ||
                blx_repair_basic(copy, sizeof copy) != 0) {
            fputs("BNS BASIC field repair failed\n", stderr);
            return 1;
        }
    }
    {
        static const unsigned char sections[] = {
            0, 240, 9, 7, 0,0,0,0,
            0, 0, 10, 7, 0,0,0,0,
            0, 32, 10, 7, 0,0,0,0,
            0, 16, 11, 7, 0,0,0,0
        };
        unsigned char copy[sizeof sections];
        memcpy(copy, sections, sizeof copy);
        copy[10] = copy[18] = 13;
        if (blx_repair_basic(copy, sizeof copy) != 2 || memcmp(copy, sections, sizeof sections)) {
            fputs("BNS BASIC line section repair failed\n", stderr);
            return 1;
        }
    }
    {
        static const unsigned char lines[] = {
            0, 0, 45, 16, 1,1,10, '"','C','o','r','r','e','c','t','.','"',
            0, 10, 45, 7, 0,0,0,0,
            0, 15, 45, 7, 0,0,0,0
        };
        unsigned char copy[sizeof lines];
        memcpy(copy, lines, sizeof copy);
        copy[6] = copy[18] = 13;
        if (blx_repair_basic(copy, sizeof copy) != 2 || memcmp(copy, lines, sizeof lines)) {
            fputs("BNS BASIC later line-number repair failed\n", stderr);
            return 1;
        }
    }
    {
        /* Two GOTO targets land on record bodies at 010Ah and 0A02h. */
        unsigned char good_addresses[6 + 320 * 8] = {0};
        unsigned char copy[sizeof good_addresses];
        unsigned i, at = 0;
        for (i = 0; i < 321; i++) {
            good_addresses[at + 1] = (unsigned char)(i + 1);
            good_addresses[at + 2] = (unsigned char)((i + 1) >> 8);
            good_addresses[at + 3] = i ? 7 : 5;
            at += i ? 8 : 6;
        }
        good_addresses[10] = good_addresses[18] = 6;
        good_addresses[11] = good_addresses[19] = 1;
        good_addresses[12] = 2; good_addresses[13] = 10;
        good_addresses[20] = 10; good_addresses[21] = 1;
        memcpy(copy, good_addresses, sizeof copy);
        copy[13] = copy[20] = 13;
        if (blx_repair_basic(copy, sizeof copy) != 2 ||
                memcmp(copy, good_addresses, sizeof copy)) {
            fputs("BNS BASIC jump repair failed\n", stderr);
            return 1;
        }
    }
    puts("BNS BASIC repair passed");
    return 0;
}
