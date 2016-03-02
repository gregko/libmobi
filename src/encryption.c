/** @file encryption.c
 *  @brief Functions to handle encryption
 *
 * Copyright (c) 2014 Bartek Fabiszewski
 * http://www.fabiszewski.net
 *
 * This file is part of libmobi.
 * Licensed under LGPL, either version 3, or any later.
 * See <http://www.gnu.org/licenses/>
 */

/* PC1 routines adapted from:

 * File PC1DEC.c
 * written in Borland Turbo C 2.0 on PC
 * PC1 Cipher Algorithm ( Pukall Cipher 1 )
 * By Alexander PUKALL 1991
 * free code no restriction to use
 * please include the name of the Author in the final software

 * Mobi encryption algorithm learned from:
 
 * mobidedrm.py
 * Copyright © 2008 The Dark Reverser
 */

#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "debug.h"
#include "encryption.h"

#define KEYVEC1 ((unsigned char*) "\x72\x38\x33\xb0\xb4\xf2\xe3\xca\xdf\x09\x01\xd6\xe2\xe0\x3f\x96")
#define KEYVEC1_V1 ((unsigned char*) "QDCVEPMU675RUBSZ")
#define PIDSIZE 10
#define KEYSIZE 16
#define COOKIESIZE 32
#define pk1_swap(a, b) { uint16_t tmp = a; a = b; b = tmp; }

/**
 @brief Structure for PK1 routines
 */
typedef struct {
    uint16_t si, x1a2, x1a0[8];
} PK1;

/**
 @brief Helper function for PK1 encryption/decryption
 
 @param[in,out] pk1 PK1 structure
 @param[in] i Iteration number
 @return PK1 inter
 */
static uint16_t pk1_code(PK1 *pk1, const uint8_t i) {
    uint16_t dx = pk1->x1a2 + i;
    uint16_t ax = pk1->x1a0[i];
    uint16_t cx = 0x015a;
    uint16_t bx = 0x4e35;
    pk1_swap(ax, pk1->si);
    pk1_swap(ax, dx);
    if (ax) { ax *= bx; }
    pk1_swap(ax, cx);
    if (ax) {
        ax *= pk1->si;
        cx += ax;
    }
    pk1_swap(ax, pk1->si);
    ax *= bx;
    dx += cx;
    ax += 1;
    pk1->x1a2 = dx;
    pk1->x1a0[i] = ax;
    return ax ^ dx;
}

/**
 @brief Helper function for PK1 encryption/decryption
 
 @param[in,out] pk1 PK1 structure
 @param[in] key 128-bit key
 @return PK1 inter
 */
static uint16_t pk1_assemble(PK1 *pk1, const unsigned char key[KEYSIZE]) {
    pk1->x1a0[0] = (key[0] * 256) + key[1];
    uint16_t inter = pk1_code(pk1, 0);
    for (uint8_t i = 1; i < (KEYSIZE / 2); i++) {
        pk1->x1a0[i] = pk1->x1a0[i - 1] ^ ((key[i * 2] * 256) + key[i * 2 + 1]);
        inter ^= pk1_code(pk1, i);
    }
    return inter;
}

/**
 @brief Decrypt buffer with PK1 algorithm
 
 @param[in,out] out Decrypted buffer
 @param[in] in Encrypted buffer
 @param[in] length Buffer length
 @param[in] key Key
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
static MOBI_RET mobi_pk1_decrypt(unsigned char *out, const unsigned char *in, size_t length, const unsigned char key[KEYSIZE]) {
    if (!out || !in) {
        return MOBI_INIT_FAILED;
    }
    unsigned char key_copy[KEYSIZE];
    memcpy(key_copy, key, KEYSIZE);
    PK1 *pk1 = calloc(1, sizeof(PK1));
    while (length--) {
        uint16_t inter = pk1_assemble(pk1, key_copy);
        uint8_t cfc = inter >> 8;
        uint8_t cfd = inter & 0xff;
        uint8_t c = *in++;
        c ^= (cfc ^ cfd);
        for (size_t i = 0; i < KEYSIZE; i++) {
            key_copy[i] ^= c;
        }
        *out++ = c;
    }
    free(pk1);
    return MOBI_SUCCESS;
}

/**
 @brief Encrypt buffer with PK1 algorithm
 
 @param[in,out] out Decrypted buffer
 @param[in] in Encrypted buffer
 @param[in] length Buffer length
 @param[in] key Key
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
static MOBI_RET mobi_pk1_encrypt(unsigned char *out, const unsigned char *in, size_t length, const unsigned char key[KEYSIZE]) {
    if (!out || !in) {
        return MOBI_INIT_FAILED;
    }
    PK1 *pk1 = calloc(1, sizeof(PK1));
    unsigned char k[KEYSIZE];
    memcpy(k, key, KEYSIZE);
    while (length--) {
        uint16_t inter = pk1_assemble(pk1, k);
        uint8_t cfc = inter >> 8;
        uint8_t cfd = inter & 0xff;
        uint8_t c = *in++;
        for (size_t i = 0; i < KEYSIZE; i++) {
            k[i] ^= c;
        }
        c ^= (cfc ^ cfd);
        *out++ = c;
    }
    free(pk1);
    return MOBI_SUCCESS;
}

/**
 @brief Structure for parsed drm record in Record0 header
 */
typedef struct {
    uint32_t verification, size, type;
    uint8_t checksum;
    unsigned char *cookie;
} MOBIDrm;

/**
 @brief Read drm records from Record0 header
 
 @param[in,out] drm MOBIDrm structure will hold parsed data
 @param[in] m MOBIData structure with raw data and metadata
 @return Number of parsed records
 */
static size_t mobi_drm_parse(MOBIDrm **drm, const MOBIData *m) {
    if (!m || !m->mh) {
        return 0;
    }
    uint32_t offset = *m->mh->drm_offset;
    uint32_t count = *m->mh->drm_count;
    uint32_t size = *m->mh->drm_size;
    if (offset == MOBI_NOTSET || count == 0) {
        return 0;
    }
    /* First record */
    MOBIPdbRecord *rec = m->rec;
    MOBIBuffer *buf = buffer_init_null(rec->size);
    if (buf == NULL) {
        debug_print("%s\n", "Memory allocation failed");
        return 0;
    }
    buf->data = rec->data;
    if (offset + size > rec->size) {
        buffer_free_null(buf);
        return 0;
    }
    buffer_setpos(buf, offset);
    for (size_t i = 0; i < count; i++) {
        drm[i] = calloc(1, sizeof(MOBIDrm));
		if (drm[i] == NULL) {
			debug_print("%s\n", "Memory allocation failed");
			buffer_free_null(buf);
			return 0;
		}
        drm[i]->verification = buffer_get32(buf);
        drm[i]->size = buffer_get32(buf);
        drm[i]->type = buffer_get32(buf);
        drm[i]->checksum = buffer_get8(buf);
        buffer_seek(buf, 3);
        drm[i]->cookie = buffer_getpointer(buf, COOKIESIZE);
    }
    buffer_free_null(buf);
    return count;
}

/**
 @brief Calculate checksum for key
 
 @param[in] key Key
 @return Checksum
 */
static uint8_t mobi_drm_keychecksum(const unsigned char key[KEYSIZE]) {
    size_t sum = 0;
    for (size_t i = 0; i < KEYSIZE; i++) {
        sum += key[i];
    }
    return (uint8_t) sum;
}

/**
 @brief Free MOBIDrm structure
 
 @param[in] drm MOBIDrm structure
 */
static void mobi_drm_free(MOBIDrm **drm, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(drm[i]);
    }
    free(drm);
}

/**
 @brief Verify decrypted cookie
 
 @param[in,out] drm_verification Checksum from drm header
 @param[in] cookie Decrypted cookie
 @return True if verification succeeds, false otherwise
 */
static bool mobi_drm_verify(const uint32_t drm_verification, const unsigned char cookie[COOKIESIZE]) {
    uint32_t verification = (uint32_t) cookie[0] << 24;
    verification |= (uint32_t) cookie[1] << 16;
    verification |= (uint32_t) cookie[2] << 8;
    verification |= cookie[3];
    uint32_t flags = (uint32_t) cookie[4] << 24;
    flags |= (uint32_t) cookie[5] << 16;
    flags |= (uint32_t) cookie[6] << 8;
    flags |= cookie[7];
    if (verification == drm_verification && (flags & 0x1f)) {
        return true;
    }
    /* FIXME: check expiry dates: two last longs of cookie */
    return false;
}

/**
 @brief Get key corresponding to given pid (encryption type 2)
 
 @param[in,out] key Key
 @param[in] pid PID
 @param[in] m MOBIData structure with raw data and metadata
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
static MOBI_RET mobi_drm_getkey_v2(unsigned char key[KEYSIZE], const unsigned char *pid, const MOBIData *m) {
    unsigned char pid_nocrc[KEYSIZE] = "\0";
    memcpy(pid_nocrc, pid, PIDSIZE - 2);
    unsigned char tempkey[KEYSIZE];
    MOBI_RET ret = mobi_pk1_encrypt(tempkey, pid_nocrc, KEYSIZE, KEYVEC1);
    if (ret != MOBI_SUCCESS) {
        return ret;
    }
    uint8_t tempkey_checksum = mobi_drm_keychecksum(tempkey);
    /* default key, no pid required */
    uint8_t keyvec1_checksum = mobi_drm_keychecksum(KEYVEC1);
    MOBIDrm **drm = malloc(*m->mh->drm_count * sizeof(MOBIDrm*));
    if (drm == NULL) {
        debug_print("Memory allocation failed%s", "\n")
        return MOBI_MALLOC_FAILED;
    }
    size_t drm_count = mobi_drm_parse(drm, m);
    for (size_t i = 0; i < drm_count; i++) {
        if (drm[i]->checksum == tempkey_checksum) {
            unsigned char cookie[COOKIESIZE];
            ret = mobi_pk1_decrypt(cookie, drm[i]->cookie, COOKIESIZE, tempkey);
            if (ret != MOBI_SUCCESS) {
                mobi_drm_free(drm, drm_count);
                return ret;
            }
            if (mobi_drm_verify(drm[i]->verification, cookie)) {
                memcpy(key, &cookie[8], KEYSIZE);
                mobi_drm_free(drm, drm_count);
                return MOBI_SUCCESS;
            }
        } else if (drm[i]->checksum == keyvec1_checksum) {
            /* try to decrypt with KEYVEC1 */
            unsigned char cookie[COOKIESIZE];
            ret = mobi_pk1_decrypt(cookie, drm[i]->cookie, COOKIESIZE, KEYVEC1);
            if (ret != MOBI_SUCCESS) {
                mobi_drm_free(drm, drm_count);
                return ret;
            }
            if (mobi_drm_verify(drm[i]->verification, cookie)) {
                memcpy(key, &cookie[8], KEYSIZE);
                mobi_drm_free(drm, drm_count);
                return MOBI_SUCCESS;
            }
        }
    }
    mobi_drm_free(drm, drm_count);
    return MOBI_DRM_KEYNOTFOUND;
}

/**
 @brief Get key corresponding for encryption type 1
 
 @param[in,out] key Key
 @param[in] m MOBIData structure with raw data and metadata
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
MOBI_RET mobi_drm_getkey_v1(unsigned char key[KEYSIZE], const MOBIData *m) {
    if (m == NULL || m->ph == NULL) {
        return MOBI_DATA_CORRUPT;
    }
    /* First record */
    MOBIPdbRecord *rec = m->rec;
    MOBIBuffer *buf = buffer_init_null(rec->size);
    buf->data = rec->data;
    if (strcmp(m->ph->type, "TEXt") == 0 && strcmp(m->ph->creator, "REAd") == 0) {
        /* offset 14 */
        buffer_setpos(buf, 14);
    } else if (m->mh == NULL || m->mh->version == NULL || *m->mh->version == MOBI_NOTSET) {
        /* offset 144 */
        buffer_setpos(buf, 144);
    } else {
        /* offset header + 16 */
        if (m->mh == NULL) {
            return MOBI_DATA_CORRUPT;
        }
        buffer_setpos(buf, *m->mh->header_length + 16);
    }
    unsigned char key_enc[KEYSIZE];
    buffer_getraw(key_enc, buf, KEYSIZE);
    buffer_free_null(buf);
    MOBI_RET ret = mobi_pk1_decrypt(key, key_enc, KEYSIZE, KEYVEC1_V1);
    return ret;
}

/**
 @brief Get key corresponding to given pid
 
 @param[in,out] key Key
 @param[in] pid PID
 @param[in] m MOBIData structure with raw data and metadata
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
static MOBI_RET mobi_drm_getkey(unsigned char key[KEYSIZE], const unsigned char *pid, const MOBIData *m) {
    MOBI_RET ret;
    if (m->rh && m->rh->encryption_type == 1) {
        ret = mobi_drm_getkey_v1(key, m);
    } else {
        if (pid[0] == '\0') {
            return MOBI_INIT_FAILED;
        }
        ret = mobi_drm_getkey_v2(key, pid, m);
    }
    return ret;
}

/**
 @brief Decrypt buffer with PK1 algorithm
 
 @param[in,out] out Decrypted buffer
 @param[in] in Encrypted buffer
 @param[in] length Buffer length
 @param[in] m MOBIData structure with loaded key
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
MOBI_RET mobi_decrypt(unsigned char *out, const unsigned char *in, const size_t length, const MOBIData *m) {
    if (m == NULL || m->drm_key == NULL) {
        return MOBI_INIT_FAILED;
    }
    MOBI_RET ret = mobi_pk1_decrypt(out, in, length, m->drm_key);
    return ret;
}

/**
 @brief Verify PID
 
 @param[in] pid PID
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
static MOBI_RET mobi_drm_pidverify(const unsigned char *pid) {
    char map[] = "ABCDEFGHIJKLMNPQRSTUVWXYZ123456789";
    uint8_t map_length = sizeof(map) - 1;
    uint32_t crc = (uint32_t) ~m_crc32(0xffffffff, pid, PIDSIZE - 2);
    crc ^= (crc >> 16);
    char checksum[2];
    for (size_t i = 0; i < 2; i++){
        uint8_t b = crc & 0xff;
        uint8_t pos = (b / map_length) ^ (b % map_length);
        checksum[i] = map[pos % map_length];
        crc >>= 8;
    }
    if (memcmp(checksum, &pid[8], 2) == 0) {
        return MOBI_SUCCESS;
    }
    return MOBI_DRM_PIDINV;
}

/**
 @brief Store key for encryption in MOBIData stucture
 
 @param[in,out] m MOBIData structure with raw data and metadata
 @param[in] pid PID
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
MOBI_RET mobi_drm_setkey_internal(MOBIData *m, const char *pid) {
    if (m == NULL || m->rh == NULL) {
        return MOBI_INIT_FAILED;
    }
    MOBI_RET ret;
    if (m->rh->encryption_type == 0) {
        debug_print("Document not encrypted%s", "\n");
        return MOBI_SUCCESS;
    }
    unsigned char drm_pid[PIDSIZE] = "\0";
    if (m->rh->encryption_type > 1) {
        if (pid == NULL) {
            return MOBI_INIT_FAILED;
        }
        const size_t pid_length = strlen(pid);
        if (pid_length != PIDSIZE) {
            debug_print("PID size is wrong (%zu)\n", pid_length);
            return MOBI_DRM_PIDINV;
        }
        memcpy(drm_pid, pid, PIDSIZE);
        ret = mobi_drm_pidverify(drm_pid);
        if (ret != MOBI_SUCCESS) {
            debug_print("PID is invalid%s", "\n")
            return ret;
        }
    } else {
        /* PID not needed */
        debug_print("Encryption doesn't require PID%s", "\n");
    }
    unsigned char key[KEYSIZE];
    ret = mobi_drm_getkey(key, drm_pid, m);
    if (ret != MOBI_SUCCESS) {
        debug_print("Key not found%s", "\n")
        return ret;
    }
    if (m->drm_key == NULL) {
        m->drm_key = malloc(KEYSIZE);
        if (m->drm_key == NULL) {
            debug_print("Memory allocation failed%s", "\n")
            return MOBI_MALLOC_FAILED;
        }
    }
    memcpy(m->drm_key, key, KEYSIZE);
    return MOBI_SUCCESS;
}

/**
 @brief Remove key from MOBIData structure
 
 @param[in,out] m MOBIData structure with raw data and metadata
 @return MOBI_RET status code (on success MOBI_SUCCESS)
 */
MOBI_RET mobi_drm_delkey_internal(MOBIData *m) {
    if (m == NULL) {
        return MOBI_INIT_FAILED;
    }
    if (m->drm_key) {
        free(m->drm_key);
    }
    m->drm_key = NULL;
    return MOBI_SUCCESS;
}

