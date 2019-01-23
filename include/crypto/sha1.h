#ifndef __SCL_sha1__
#define __SCL_sha1__

typedef unsigned char uschar;

typedef struct _sha1 {
    unsigned int H[5];
    unsigned int length;
}sha1;

void sha1_start(sha1 *base);
void sha1_mid(sha1 *base, const uschar *text);
void sha1_end(sha1 *base, const uschar *text, int length, uschar *digest);

#endif
