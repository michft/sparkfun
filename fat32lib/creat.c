#include "fat32private.h"

// sequence number for next auto log file name;
static u32 nextlog = 0;

unsigned char newextension[4] = "LOG";

static void clearclus(u32 clus)
{
    u16 sect;
    tzmemclr(filesectbuf, 512);
    for (sect = 0; sect < fpm.spc; sect++)
        writesec(clus2sec(clus) + sect);
}

static void ungetclus(u32 clus)
{
    readsec(fpm.fat0 + (clus >> 7));
    tzmemclr(&filesectbuf[(127 & clus) << 2], 0);
    u8 i;
    for (i = 0; i < fpm.nft; i++)
        writesec(fpm.fat0 + (clus >> 7) + i * fpm.spf);
}

static int findemptydirent(u8 * dosname)
{
    u32 iclus = dirclus;
    if (!iclus)                 // following a .. back to root
        iclus = fpm.d0c;

    direntsect = 0;
    do {                        // more clusters
        u16 secinclus;
        u32 thissector;
        for (secinclus = 0; secinclus < fpm.spc; secinclus++) { // sector within cluster
            thissector = clus2sec(iclus) + secinclus;
            if (readsec(thissector))
                return -1;
            u8 *c, i;
            for (i = 0; i < 16; i++) {
                c = &filesectbuf[i << 5];
                if (!direntsect && (*c == 0xe5 || !*c)) {       // EOD
                    direntsect = thissector;
                    direntnum = i;
                }
                if (!*c)
                    return 0;
                if (c[11] == 0x0f)      // long name ent
                    continue;
                if (!dosname) {
                    if (c[8] == newextension[0] && c[9] == newextension[1] && c[10] == newextension[2]) {
                        u32 thislog = 0;
                        u16 j, k;
                        for (j = 0; j < 8; j++) {
                            if (c[j] < '0' || c[j] > '9') {
                                thislog = 0xffffffff;
                                break;
                            }
                            k = c[j] - '0';
                            thislog <<= 4;
                            thislog |= k;
                        }
                        if (thislog != 0xffffffff) {
                            if (thislog >= nextlog)
                                nextlog = thislog + 1;
                        }
                    }
                } else if (!tzfncmp(c, dosname))
                    return 2;   // duplicate
            }
        }
        iclus = nextclus(iclus);
    } while (iclus < 0xffffff0);
    // TODO:
    // EOD at boundary - need to extend
    return 1;
}

static void setdirent(u8 * c, u8 attr, u32 clus)
{
    c[11] = attr;
    // date - jan 2010
    c[16] = c[18] = c[24] = 0x21;
    c[17] = c[19] = c[25] = 0x3c;
    c[26] = clus;
    c[27] = clus >> 8;
    c[20] = clus >> 16;
    c[21] = clus >> 24;
    if (!(attr & 0x18)) {       // if not dir or label
        u32 t = fpm.spc << 9;
        c[28] = t;
        c[29] = t >> 8;
        c[30] = t >> 16;
        c[31] = t >> 24;
    }
}

static u32 getclus()
{
    u32 clus = 0, newclus, curr = 1;
    u8 *c;

    newclus = currclus + 1;
    while (newclus < fpm.mxcl) {
        readsec(fpm.fat0 + (newclus >> 7));     // read sector with cluster
        clus = newclus & 127;   // index within sector
        c = &filesectbuf[clus << 2];
        do {
            get4todw(curr);     // get linked cluster
        } while (curr && ++clus < 128);
        newclus &= ~127;
        if (clus < 128)
            break;
        newclus += 128;
    }
    if (newclus >= fpm.mxcl)
        return 0;
    newclus += clus;
    c = &filesectbuf[clus << 2];
    *c++ = 0xff;
    *c++ = 0xff;
    *c++ = 0xff;
    *c = 0x0f;
    // can merge link here for that case - saves extra read and write if in same clus
    u8 i;
    for (i = 0; i < fpm.nft; i++)
        writesec(fpm.fat0 + (newclus >> 7) + i * fpm.spf);

    secinclus = 0;
    byteinsec = 0;

    return newclus;
}

int newdirent(u8 * dosname, u8 attr)
{
    int r;
    currclus = getclus();

    if ((r = findemptydirent(dosname))) {
        rmw = 0;                // no reads
        ungetclus(currclus);
        currclus = 0;
        return r;
    }
    ireadclus = currclus;

    u8 *c;
    c = &filesectbuf[direntnum << 5];

    if (*c && *c != 0xe5)
        return -1;              // ERROR SHOULDN'T HAPPEN

    tzmemclr(c, 32);
    if (!dosname) {
        // replace nextlog with template
        u32 t = nextlog;
        u8 i = 8, j;
        c += 8;
        tzmemcpy(c, newextension, 3);
        while (i--) {
            j = t & 15;
#ifndef HEXFN
            if( j > 9 )
                j -= 10, t += 6;
            t >>= 4;
            *--c = '0' + j;
#else
            t >>= 4;
            *--c = j < 10 ? '0' + j : 'A' - 10 + j;
#endif
        }
    } else
        tzmemcpy(c, dosname, 11);

    setdirent(c, attr, currclus);
    longentsect = 0;

    writesec(direntsect);

    if (attr & 0x10) {          // setup new directory
        clearclus(currclus);
        tzmemclr(filesectbuf, 512);
        filesectbuf[0] = filesectbuf[32] = filesectbuf[33] = '.';
        tzmemset(&filesectbuf[1], ' ', 10);
        tzmemset(&filesectbuf[34], ' ', 9);
        setdirent(filesectbuf, 0x10, currclus);
        if (dirclus == fpm.d0c)
            dirclus = 0;
        setdirent(&filesectbuf[32], 0x10, dirclus);
        dirclus = currclus;
        currclus = 0;
        writesec(clus2sec(dirclus));
    }
    byteinsec = 0;
    secinclus = 0;
    totfsiz = 0;
    fdisplacement = 0;
    return 0;
}
