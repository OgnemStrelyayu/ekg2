/*
 *  (C) Copyright 2007 Jakub Zawadzki <darkjames@darkjames.ath.cx>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* gg_login_hash() copied from libgadu copyrighted under LGPL-2.1 (C) libgadu developers */

/*
SHA-1 in C
By Steve Reid <steve@edmweb.com>
100% Public Domain

Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

/* !!! ten kod tutaj dziala tylko na 32b LE !!! */

#define DIGIT_SIZE (sizeof(digit)-2)	/* glupie gcc, i konczenie stringow \0 :( */ 
// static const char digit[] = "\0abcdefghijklmnoprstuwxyzABCDEFGHIJKLMOPRSTUWXYZ1234567890";
static const char digit[] = "\0abcdefghijklmnoprstuwxyz";	/* bo kto tak naprawde korzysta z trudnych hasel? */

#define MAX_PASS_LEN 15	/* dlugosc hasla, tak naprawde to jest+1, nie przejmowac sie. */

#define ULTRA_DEBUG 	0	/* sprawdza czy dobrze generujemy hasla (w/g digit, b. niepotrzebne i b. wolne) */
#define ULTRA_VERBOSE	0	/* rysuje kropki */
#define ULTRA_SAFE	0	/* sprawdza czy nie bedziemy rysowac po pamieci jesli haslo zacznie miec wiecej niz MAX_PASS_LEN znakow */
#define ULTRA_CACHE 	0	/* XXX keszuje wyniki, jak komus sie chce napisac. */

static unsigned char pass[MAX_PASS_LEN];
static size_t pass_pos = 0;

#if ULTRA_CACHE
typedef struct {
	unsigned int x;
	unsigned int y;
} last_t;

static last_t lasts[MAX_PASS_LEN];
static last_t *last_last;

#endif

/* SHA1 STUFF */

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) \
    |(rol(block->l[i],8)&0x00FF00FF))
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] \
    ^block->l[(i+2)&15]^block->l[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);


/* XXX, ?SHA-1 Broken?, XXX */
static inline int gg_login_sha1hash(const unsigned char *password, const size_t passlen, const uint32_t seed, const uint32_t *dig) {
	int i;

	uint32_t state[5];
	unsigned char buffer[64];

/* SHA1Init() */
    /* SHA1 initialization constants */
	state[0] = 0x67452301;
	state[1] = 0xEFCDAB89;
	state[2] = 0x98BADCFE;
	state[3] = 0x10325476;
	state[4] = 0xC3D2E1F0;

	/* XXX, it's optimized but it'll work only for short passwords, shorter than 63-4-7 */
	{
		for (i = 0; i < passlen; i++) 
			buffer[i] = digit[password[i]];

		memcpy(&buffer[passlen], &seed, 4);
	}

/* SHA1Final() */
	/* Add padding and return the message digest. */
	{
	/* pad */
		buffer[passlen+4] = '\200';
		for (i = passlen+5; i < 63-7; i++)
			buffer[i] = '\0';
			
	/* finalcount */
		for (i = 63-7; i < 63; i++)
			buffer[i] = '\0';

		buffer[63] = (unsigned char) (((passlen+4) << 3) & 0xff);
	}
/* SHA1Transform() */
	/* Hash a single 512-bit block. This is the core of the algorithm. */
	{
		typedef union {
			unsigned char c[64];
			uint32_t l[16];
		} CHAR64LONG16;

		CHAR64LONG16* block = (CHAR64LONG16*) buffer;

		/* Copy context->state[] to working vars */
		uint32_t a = state[0];
		uint32_t b = state[1];
		uint32_t c = state[2];
		uint32_t d = state[3];
		uint32_t e = state[4];

		/* 4 rounds of 20 operations each. Loop unrolled. */
		R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
		R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
		R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
		R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
		R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
		R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
		R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
		R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
		R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
		R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
		R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
		R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
		R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
		R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
		R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
		R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
		R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
		R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
		R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
		R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

		/* Add the working vars back into context.state[] */
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
	}

#if ULTRA_DEBUG
	for (password = pass; *password; password++) {
		printf("%c", digit[*password]);
	}
	printf(" -> %.8x%.8x%.8x%.8x%.8x\n", state[0], state[1], state[2], state[3], state[4]);
#endif

/* it returns 0 if digest match, 1 if not */
	for (i = 0; i < 5; i++)
		if (dig[i] != state[i])
			return 1;
	return 0;
}

/* stolen from libgadu */
static inline unsigned int gg_login_hash(const unsigned char *password, unsigned int y /* seed */
#if ULTRA_CACHE
, unsigned int x
#endif
) {
#if !ULTRA_CACHE
	unsigned int x = 0;
#endif
	unsigned int z;

/* I have no idea, how to crack/optimize this algo. Maybe someone? */
	for (; *password; password++) {
		x = (x/* & 0xffffff00 */) | digit[*password]; /* LE x86 32b, po co & ? */
		y ^= x;
		y += x;
		x <<= 8;
		y ^= x;
		x <<= 8;
		y -= x;
		x <<= 8;
		y ^= x;

		z = y & 0x1F;
		y = (y << z) | (y >> (32 - z));
	}

#if ULTRA_DEBUG
	for (password = pass; *password; password++) {
		printf("%c", digit[*password]);
	}
	printf(" -> 0x%x\n", y);
#endif
	return y;
}
  

static void bonce(unsigned char *buf, size_t len) {
	size_t i;
	for (i = 0; i < len; i++) 
		buf[i] = 1;
}

static void print_pass(unsigned char *pass) {
	printf("Collision found: ");
	while (*pass) {
		putchar(digit[*pass]);

		pass++;
	}
	printf("\n");
}
static inline void incr() {
	int i;

	for (i = pass_pos; i >= 0; i--) {
		if (pass[i] < DIGIT_SIZE) {
#if ULTRA_VERBOSE
			/* jesli ktos bardzo lubi kropki, lepiej jest nie lubiec. */
			if (i == 0) {
				putchar('.');
				fflush(stdout);
			}
#endif
			pass[i]++;
			bonce(&(pass[i+1]), pass_pos-i);
			return;
		}
	}
#if ULTRA_SAFE 
	/* po co to komu? */
	if (pass_pos == MAX_PASS_LEN) {
		fprintf(stderr, "%s:%d pass_pos == MAX_PASS_LEN, incr MAX_PASS_LEN?\n", __FILE__, __LINE__);
		exit(1);
	}
#endif
	pass_pos++;
	printf("Len: %d\n", pass_pos+1);

	bonce(pass, pass_pos+1);
}


/* sample, smb has this seed/hash. 
 *	change it to your data.
 *
 *	you can benchmark && check if you gg-keygen.c generate good data with it.
 *	tested under athlon-xp 2500 XP+
 */

#if 0	/* kasza */	/*   0.193s */
#define SEED 0xe2cf3809
#define HASH 0x4d940c10
#endif

#if 0	/* agakrht */	/* 14.738s */
#define SEED 0xd3c742b6
#define HASH 0x9f9b9205
#endif

/* with apended '2' to digit: [static const char digit[] = "\0abcdefghijklmnoprstuwxyz2"] */
#if 0	/* qwerty2 */	/* */
#define SEED 0xb2b9eec8
#define HASH_SHA1 "a266db74a7289913ec30a7872b7384ecc119e4ec"
#endif

#define NOT_STOP_ON_FIRST 0

int main() {
#ifdef HASH_SHA1
	unsigned char digest[20];
	uint32_t digstate[5];
	int i;

/* HASH w SHA1 najpierw z 40 znakowego, ascii-printable znakow od [0-f] zamieniamy na binarna 20 znakow tablice.. */

	for (i = 0; i < 40; i++) {
		uint8_t znak;

		if (HASH_SHA1[i] == '\0') { fprintf(stderr, "BAD SHA1 hash: %s\n", HASH_SHA1);	return 1; }

		if (tolower(HASH_SHA1[i]) >= 'a' && tolower(HASH_SHA1[i]) <= 'f')
			znak = 10 + tolower(HASH_SHA1[i]-'a');
		else if (HASH_SHA1[i] >= '0' && HASH_SHA1[i] <= '9')
			znak = HASH_SHA1[i]-'0';
		else { fprintf(stderr, "BAD SHA1 char!\n"); return 1; }

		if (i % 2)
			digest[i / 2] |= znak;
		else	digest[i / 2] = znak << 4;

	}
	if (HASH_SHA1[40] != '\0') { fprintf(stderr, "BAD SHA1 hash: %s\n", HASH_SHA1);  return 1; }

	printf("%s == ", HASH_SHA1);
	for (i = 0; i < 20; i++) 
		printf("%.2x", digest[i]);

	printf("\n");

/* a teraz zamienmy na tablice 32bitowych liczb.. */
	digstate[0] = digest[ 0] << 24 | digest[ 1] << 16 | digest[ 2] << 8 | digest[ 3];
	digstate[1] = digest[ 4] << 24 | digest[ 5] << 16 | digest[ 6] << 8 | digest[ 7];
	digstate[2] = digest[ 8] << 24 | digest[ 9] << 16 | digest[10] << 8 | digest[11];
	digstate[3] = digest[12] << 24 | digest[13] << 16 | digest[14] << 8 | digest[15];
	digstate[4] = digest[16] << 24 | digest[17] << 16 | digest[18] << 8 | digest[19];
#endif
	
	memset(pass, 0, sizeof(pass));
	do {
		do {
			incr();
		} while 
#ifdef HASH_SHA1
			(gg_login_sha1hash(pass, pass_pos+1, SEED, digstate));
#else
			((hash = gg_login_hash(pass, SEED
	#if ULTRA_CACHE
						,0
	#endif
					      )) != HASH);
#endif
		print_pass(pass);
	} while(NOT_STOP_ON_FIRST);
	return 0;
}

