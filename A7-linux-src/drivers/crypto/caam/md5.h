#ifndef     __MD5_H__
#define     __MD5_H__


/* MD5 context. */
typedef struct {
    u32 state[4];          /* state (ABCD) */
    u32 count[2];          /* number of bits, modulo 2^64 (lsb first) */
    unsigned char buffer[64];   /* input buffer */
} MD5_CTX;

void MD5Init (
        MD5_CTX *context,
        u32 A,
        u32 B,
        u32 C,
        u32 D);
void MD5Update (
        MD5_CTX *context,
        const uint8_t *input,
        u32 inputLen
        );
void MD5Final  (
        MD5_CTX *context,
        unsigned char [16]);

#endif
