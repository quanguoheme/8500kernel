/* $Cambridge: exim/exim-src/src/auths/sha1.c,v 1.4 2007/01/08 10:50:19 ph10 Exp $ */

/*************************************************
*     Exim - an Internet mail transport agent    *
*************************************************/

/* Copyright (c) University of Cambridge 1995 - 2007 */
/* See the file NOTICE for conditions of use and distribution. */

#include <linux/string.h>
#include "sha1.h"

/*************************************************
*        Start off a new SHA-1 computation.      *
*************************************************/

/*
Argument:  pointer to sha1 storage structure
Returns:   nothing
*/

void
sha1_start(sha1 *base)
{
    base->H[0] = 0x67452301;
    base->H[1] = 0xefcdab89;
    base->H[2] = 0x98badcfe;
    base->H[3] = 0x10325476;
    base->H[4] = 0xc3d2e1f0;
    base->length = 0;
}

/*************************************************
*       Process another 64-byte block            *
*************************************************/

/* This function implements central part of the algorithm

Arguments:
  base       pointer to sha1 storage structure
  text       pointer to next 64 bytes of subject text

Returns:     nothing
*/

void
sha1_mid(sha1 *base, const uschar *text)
{
    register int i;
    unsigned int A, B, C, D, E;
    unsigned int W[80];

    base->length += 64;

    for (i = 0; i < 16; i++)
    {
        W[i] = (text[0] << 24) | (text[1] << 16) | (text[2] << 8) | text[3];
        text += 4;
    }

    for (i = 16; i < 80; i++)
    {
        register unsigned int x = W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16];
        W[i] = (x << 1) | (x >> 31);
    }

    A = base->H[0];
    B = base->H[1];
    C = base->H[2];
    D = base->H[3];
    E = base->H[4];

    for (i = 0; i < 20; i++)
    {
        unsigned int T;
        T = ((A << 5) | (A >> 27)) + ((B & C) | ((~B) & D)) + E + W[i] + 0x5a827999;
        E = D;
        D = C;
        C = (B << 30) | (B >> 2);
        B = A;
        A = T;
    }

    for (i = 20; i < 40; i++)
    {
        unsigned int T;
        T = ((A << 5) | (A >> 27)) + (B ^ C ^ D) + E + W[i] + 0x6ed9eba1;
        E = D;
        D = C;
        C = (B << 30) | (B >> 2);
        B = A;
        A = T;
    }

    for (i = 40; i < 60; i++)
    {
        unsigned int T;
        T = ((A << 5) | (A >> 27)) + ((B & C) | (B & D) | (C & D)) + E + W[i] +
            0x8f1bbcdc;
        E = D;
        D = C;
        C = (B << 30) | (B >> 2);
        B = A;
        A = T;
    }

    for (i = 60; i < 80; i++)
    {
        unsigned int T;
        T = ((A << 5) | (A >> 27)) + (B ^ C ^ D) + E + W[i] + 0xca62c1d6;
        E = D;
        D = C;
        C = (B << 30) | (B >> 2);
        B = A;
        A = T;
    }

    base->H[0] += A;
    base->H[1] += B;
    base->H[2] += C;
    base->H[3] += D;
    base->H[4] += E;
}

/*************************************************
*     Process the final text string              *
*************************************************/

/* The string may be of any length. It is padded out according to the rules
for computing SHA-1 digests. The final result is then converted to text form
and returned.

Arguments:
  base      pointer to the sha1 storage structure
  text      pointer to the final text vector
  length    length of the final text vector
  digest    points to 16 bytes in which to place the result

Returns:    nothing
*/

void
sha1_end(sha1 *base, const uschar *text, int length, uschar *digest)
{
    int i;
    uschar work[64];

    /* Process in chunks of 64 until we have less than 64 bytes left. */

    while (length >= 64)
    {
        sha1_mid(base, text);
        text += 64;
        length -= 64;
    }

    /* If the remaining string contains more than 55 bytes, we must pad it
    out to 64, process it, and then set up the final chunk as 56 bytes of
    padding. If it has less than 56 bytes, we pad it out to 56 bytes as the
    final chunk. */

    memcpy(work, text, length);
    work[length] = 0x80;

    if (length > 55)
    {
        memset(work+length+1, 0, 63-length);
        sha1_mid(base, work);
        base->length -= 64;
        memset(work, 0, 56);
    }
    else
    {
        memset(work+length+1, 0, 55-length);
    }

    /* The final 8 bytes of the final chunk are a 64-bit representation of the
    length of the input string *bits*, before padding, high order word first, and
    high order bytes first in each word. This implementation is designed for short
    strings, and so operates with a single int counter only. */

    length += base->length;   /* Total length in bytes */
    length <<= 3;             /* Total length in bits */

    work[63] = length         & 0xff;
    work[62] = (length >>  8) & 0xff;
    work[61] = (length >> 16) & 0xff;
    work[60] = (length >> 24) & 0xff;

    memset(work+56, 0, 4);

    /* Process the final 64-byte chunk */

    sha1_mid(base, work);

    /* Pass back the result, high-order byte first in each word. */

    for (i = 0; i < 5; i++)
    {
        register int x = base->H[i];
        *digest++ = (x >> 24) & 0xff;
        *digest++ = (x >> 16) & 0xff;
        *digest++ = (x >>  8) & 0xff;
        *digest++ =  x        & 0xff;
    }
}

