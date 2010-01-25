#include "fat32private.h"
#include <stdio.h>

void syncdirent(u8 maxout)
{
    if (!direntsect || readsec(direntsect))
        return;
    u8 *c;
    c = &filesectbuf[direntnum << 5];
    if (fdisplacement > totfsiz)
        totfsiz = fdisplacement;
    u32 curr, t;
    t = totfsiz;
    // this will cause the size to be truncated to the last cluster boundary
    // on an fsck preserving written data in case of a crash
    if (maxout)
        t += fpm.spc << 9;      // add a cluster to current
    // it may already be the correct value
    c += 28;
    get4todw(curr);             // get linked cluster
    if (curr == totfsiz)
        return;
    // update
    c = &filesectbuf[direntnum << 5];

    c[28] = totfsiz;
    c[29] = totfsiz >> 8;
    c[30] = totfsiz >> 16;
    c[31] = totfsiz >> 24;
    writesec(direntsect);
}

u32 getclus()
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

static void linkclus()
{
    u32 nextc;

    nextc = nextclus(currclus);
    if (nextc < 0xffffff0)
        return;

    nextc = getclus();
    if (!nextc) {
        currclus = 0;
        return;
    }
    // redundant if alloc and next are in same FAT sector
    readsec(fpm.fat0 + (currclus >> 7));        // read sector with cluster
    u8 *c = &filesectbuf[(currclus & 127) << 2];
    // should be 0x0fffffff;
    *c++ = nextc;
    *c++ = nextc >> 8;
    *c++ = nextc >> 16;
    *c = nextc >> 24;

    u8 i;
    for (i = 0; i < fpm.nft; i++)
        writesec(fpm.fat0 + (currclus >> 7) + i * fpm.spf);
    currclus = nextc;
}

void flushbuf()
{
    //if ! dirty return
    // if byteinsec &&?
    if (currclus > 1)
        writesec(clus2sec(currclus) + secinclus);
    if (totfsiz < fdisplacement)
        totfsiz = fdisplacement;
}

int writenextsect()
{
#ifndef NOBLOCKWRITEDIRS
    if (dirclus == ireadclus)
        return -1;
#endif
    if (currclus < 2)
        return -1;
    writesec(clus2sec(currclus) + secinclus++); // FIXME need to return -2 on error
    fdisplacement += 512;
    fdisplacement &= ~511;
    if( !fdisplacement ) { // add 512 and get zero?
        fdisplacement--;
        return 2; // EOF - size is maxed out.
    }
    byteinsec = 0;

    if (totfsiz < fdisplacement)
        totfsiz = fdisplacement;

    //NOTE - this will allocate a completely empty cluster if EOF just happened
    if (secinclus >= fpm.spc) {
#if 0
        syncdirent(0);
#endif
        linkclus();
        secinclus = 0;
        if (currclus < 2)
            return 1;           // written, but no more space
    }
    if (rmw)
        readsec(clus2sec(currclus) + secinclus);
    return 0;
}

int writebyte(u8 c)
{
    filesectbuf[byteinsec++] = c;
    if (byteinsec > 511)
        return writenextsect();
    if( fdisplacement == 0xffffffff )
        return -1;
    fdisplacement++;
    return 0;
}

// mark cluster avail and firstfree hings as unknown
// placeholder for actual proper update
void zaphint()
{
    u32 s0, s1;
    u8 *c;

    readsec(fpm.hint);
    c = &filesectbuf[488];
    get4todw(s0);
    get4todw(s1);
    if (s0 == 0xffffffff && s1 == 0xffffffff)
        return;
    c = &filesectbuf[488];
    tzmemset(c, 0xff, 8);
    writesec(fpm.hint);
}
