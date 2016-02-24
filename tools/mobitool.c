/** @file mobitool.c
 *
 * @brief mobitool
 *
 * @example mobitool.c
 * Program for testing libmobi library
 *
 * Copyright (c) 2014 Bartek Fabiszewski
 * http://www.fabiszewski.net
 *
 * Licensed under LGPL, either version 3, or any later.
 * See <http://www.gnu.org/licenses/>
 *
 * Modified slightly by Grzegorz Kochaniak, greg@hyperionics.com, in Feb. 2016 -
 * changed variable length arrays to alloca() calls, added convert to EPUB module
 * and command line option.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
# include <unistd.h>
#endif
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
/* include libmobi header */
#include <mobi.h>
#include "save_epub.h"
#ifdef HAVE_CONFIG_H
# include "../config.h"
#endif

#ifdef HAVE_SYS_RESOURCE_H
/* rusage */
# include <sys/resource.h>
# define PRINT_RUSAGE_ARG "u"
#else
# define PRINT_RUSAGE_ARG ""
#endif
/* encryption */
#ifdef USE_ENCRYPTION
# define PRINT_ENC_USG " [-p pid]"
# define PRINT_ENC_ARG "p:"
#else
# define PRINT_ENC_USG ""
# define PRINT_ENC_ARG ""
#endif
/* return codes */
#define ERROR 1
#define SUCCESS 0

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#if defined(__clang__)
# define COMPILER "clang " __VERSION__
#elif defined(__SUNPRO_C)
# define COMPILER "suncc " STR(__SUNPRO_C)
#elif defined(__GNUC__)
# if (defined(__MINGW32__) || defined(__MINGW64__))
#  define COMPILER "gcc (MinGW) " __VERSION__
# else
#  define COMPILER "gcc " __VERSION__
# endif
#else
# define COMPILER "unknown"
#endif

#if !defined S_ISDIR && defined S_IFDIR
# define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISDIR
# error "At least one of S_ISDIR or S_IFDIR macros is required"
#endif

/* command line options */
int dump_rawml_opt = 0;
int print_rec_meta_opt = 0;
int dump_rec_opt = 0;
int parse_kf7_opt = 0;
int dump_parts_opt = 0;
int dump_epub_opt = 0;
int print_rusage_opt = 0;
int outdir_opt = 0;
#ifdef USE_ENCRYPTION
int setpid_opt = 0;
#endif

/* options values */
char outdir[FILENAME_MAX];
char* epub_fn = outdir;
#ifdef USE_ENCRYPTION
char *pid = NULL;
#endif

#ifdef _WIN32
const int separator = '\\';
#else
const int separator = '/';
#endif

static int mt_mkdir (const char *filename) {
    int ret;
#ifdef _WIN32
    ret = _mkdir(filename);
#else
    ret = mkdir(filename, S_IRWXU);
#endif
    return ret;
}

/**
 @brief Parse file name into file path and base name
 @param[in] fullpath Full file path
 @param[in,out] dirname Will be set to full dirname
 @param[in,out] basename Will be set to file basename
 */
void split_fullpath(const char *fullpath, char *dirname, char *basename) {
    char *p = strrchr(fullpath, separator);
    if (p) {
        p += 1;
        strncpy(dirname, fullpath, (p - fullpath));
        dirname[p - fullpath] = '\0';
        strncpy(basename, p, strlen(p) + 1);
    }
    else {
        dirname[0] = '\0';
        strncpy(basename, fullpath, strlen(fullpath) + 1);
    }
    p = strrchr(basename, '.');
    if (p) {
        *p = '\0';
    }
}

/**
 @brief Check whether given path exists and is a directory
 @param[in] path Path to be tested
 */
bool dir_exists(const char *path) {
    struct stat sb;
    if (stat(path, &sb) != 0) {
        int errsv = errno;
        printf("Path \"%s\" is not accessible (%s)\n", path, strerror(errsv));
        return false;
    }
    else if (!S_ISDIR(sb.st_mode)) {
        printf("Path \"%s\" is not a directory\n", path);
        return false;
    }
    return true;
}

/**
 @brief Print all loaded headers meta information
 @param[in] m MOBIData structure
 */
void print_meta(const MOBIData *m) {
    /* Full name stored at offset given in MOBI header */
    if (m->mh && m->mh->full_name_offset && m->mh->full_name_length) {
        size_t len = *m->mh->full_name_length;
        char *full_name = _ALLOCA(len + 1);
        if(mobi_get_fullname(m, full_name, len) == MOBI_SUCCESS) {
            printf("\nFull name: %s\n", full_name);
        }
    }
    /* Palm database header */
    if (m->ph) {
        printf("\nPalm doc header:\n");
        printf("name: %s\n", m->ph->name);
        printf("attributes: %hu\n", m->ph->attributes);
        printf("version: %hu\n", m->ph->version);
        struct tm * timeinfo = mobi_pdbtime_to_time(m->ph->ctime);
        printf("ctime: %s", asctime(timeinfo));
        timeinfo = mobi_pdbtime_to_time(m->ph->mtime);
        printf("mtime: %s", asctime(timeinfo));
        timeinfo = mobi_pdbtime_to_time(m->ph->btime);
        printf("btime: %s", asctime(timeinfo));
        printf("mod_num: %u\n", m->ph->mod_num);
        printf("appinfo_offset: %u\n", m->ph->appinfo_offset);
        printf("sortinfo_offset: %u\n", m->ph->sortinfo_offset);
        printf("type: %s\n", m->ph->type);
        printf("creator: %s\n", m->ph->creator);
        printf("uid: %u\n", m->ph->uid);
        printf("next_rec: %u\n", m->ph->next_rec);
        printf("rec_count: %u\n", m->ph->rec_count);
    }
    /* Record 0 header */
    if (m->rh) {
        printf("\nRecord 0 header:\n");
        printf("compresion type: %u\n", m->rh->compression_type);
        printf("text length: %u\n", m->rh->text_length);
        printf("text record count: %u\n", m->rh->text_record_count);
        printf("text record size: %u\n", m->rh->text_record_size);
        printf("encryption type: %u\n", m->rh->encryption_type);
        printf("unknown: %u\n", m->rh->unknown1);
    }
    /* Mobi header */
    if (m->mh) {
        printf("\nMOBI header:\n");
        printf("identifier: %s\n", m->mh->mobi_magic);
        if(m->mh->header_length) { printf("header length: %u\n", *m->mh->header_length); }
        if(m->mh->mobi_type) { printf("mobi type: %u\n", *m->mh->mobi_type); }
        if(m->mh->text_encoding) { printf("text encoding: %u\n", *m->mh->text_encoding); }
        if(m->mh->uid) { printf("unique id: %u\n", *m->mh->uid); }
        if(m->mh->version) { printf("file version: %u\n", *m->mh->version); }
        if(m->mh->orth_index) { printf("orth index: %u\n", *m->mh->orth_index); }
        if(m->mh->infl_index) { printf("infl index: %u\n", *m->mh->infl_index); }
        if(m->mh->names_index) { printf("names index: %u\n", *m->mh->names_index); }
        if(m->mh->keys_index) { printf("keys index: %u\n", *m->mh->keys_index); }
        if(m->mh->extra0_index) { printf("extra0 index: %u\n", *m->mh->extra0_index); }
        if(m->mh->extra1_index) { printf("extra1 index: %u\n", *m->mh->extra1_index); }
        if(m->mh->extra2_index) { printf("extra2 index: %u\n", *m->mh->extra2_index); }
        if(m->mh->extra3_index) { printf("extra3 index: %u\n", *m->mh->extra3_index); }
        if(m->mh->extra4_index) { printf("extra4 index: %u\n", *m->mh->extra4_index); }
        if(m->mh->extra5_index) { printf("extra5 index: %u\n", *m->mh->extra5_index); }
        if(m->mh->non_text_index) { printf("non text index: %u\n", *m->mh->non_text_index); }
        if(m->mh->full_name_offset) { printf("full name offset: %u\n", *m->mh->full_name_offset); }
        if(m->mh->full_name_length) { printf("full name length: %u\n", *m->mh->full_name_length); }
        if(m->mh->locale) {
            const char *locale_string = mobi_get_locale_string(*m->mh->locale);
            if (locale_string) {
                printf("locale: %s (%u)\n", locale_string, *m->mh->locale);
            } else {
                printf("locale: unknown (%u)\n", *m->mh->locale);
            }
        }
        if(m->mh->dict_input_lang) {
            const char *locale_string = mobi_get_locale_string(*m->mh->dict_input_lang);
            if (locale_string) {
                printf("dict input lang: %s (%u)\n", locale_string, *m->mh->dict_input_lang);
            } else {
                printf("dict input lang: unknown (%u)\n", *m->mh->dict_input_lang);
            }
        }
        if(m->mh->dict_output_lang) {
            const char *locale_string = mobi_get_locale_string(*m->mh->dict_output_lang);
            if (locale_string) {
                printf("dict output lang: %s (%u)\n", locale_string, *m->mh->dict_output_lang);
            } else {
                printf("dict output lang: unknown (%u)\n", *m->mh->dict_output_lang);
            }
        }
        if(m->mh->min_version) { printf("minimal version: %u\n", *m->mh->min_version); }
        if(m->mh->image_index) { printf("first image index: %u\n", *m->mh->image_index); }
        if(m->mh->huff_rec_index) { printf("huffman record offset: %u\n", *m->mh->huff_rec_index); }
        if(m->mh->huff_rec_count) { printf("huffman records count: %u\n", *m->mh->huff_rec_count); }
        if(m->mh->datp_rec_index) { printf("DATP record offset: %u\n", *m->mh->datp_rec_index); }
        if(m->mh->datp_rec_count) { printf("DATP records count: %u\n", *m->mh->datp_rec_count); }
        if(m->mh->exth_flags) { printf("EXTH flags: %u\n", *m->mh->exth_flags); }
        if(m->mh->unknown6) { printf("unknown: %u\n", *m->mh->unknown6); }
        if(m->mh->drm_offset) { printf("drm offset: %u\n", *m->mh->drm_offset); }
        if(m->mh->drm_count) { printf("drm count: %u\n", *m->mh->drm_count); }
        if(m->mh->drm_size) { printf("drm size: %u\n", *m->mh->drm_size); }
        if(m->mh->drm_flags) { printf("drm flags: %u\n", *m->mh->drm_flags); }
        if(m->mh->first_text_index) { printf("first text index: %u\n", *m->mh->first_text_index); }
        if(m->mh->last_text_index) { printf("last text index: %u\n", *m->mh->last_text_index); }
        if(m->mh->fdst_index) { printf("FDST offset: %u\n", *m->mh->fdst_index); }
        if(m->mh->fdst_section_count) { printf("FDST count: %u\n", *m->mh->fdst_section_count); }
        if(m->mh->fcis_index) { printf("FCIS index: %u\n", *m->mh->fcis_index); }
        if(m->mh->fcis_count) { printf("FCIS count: %u\n", *m->mh->fcis_count); }
        if(m->mh->flis_index) { printf("FLIS index: %u\n", *m->mh->flis_index); }
        if(m->mh->flis_count) { printf("FLIS count: %u\n", *m->mh->flis_count); }
        if(m->mh->unknown10) { printf("unknown: %u\n", *m->mh->unknown10); }
        if(m->mh->unknown11) { printf("unknown: %u\n", *m->mh->unknown11); }
        if(m->mh->srcs_index) { printf("SRCS index: %u\n", *m->mh->srcs_index); }
        if(m->mh->srcs_count) { printf("SRCS count: %u\n", *m->mh->srcs_count); }
        if(m->mh->unknown12) { printf("unknown: %u\n", *m->mh->unknown12); }
        if(m->mh->unknown13) { printf("unknown: %u\n", *m->mh->unknown13); }
        if(m->mh->extra_flags) { printf("extra record flags: %u\n", *m->mh->extra_flags); }
        if(m->mh->ncx_index) { printf("NCX offset: %u\n", *m->mh->ncx_index); }
        if(m->mh->unknown14) { printf("unknown: %u\n", *m->mh->unknown14); }
        if(m->mh->unknown15) { printf("unknown: %u\n", *m->mh->unknown15); }
        if(m->mh->fragment_index) { printf("fragment index: %u\n", *m->mh->fragment_index); }
        if(m->mh->skeleton_index) { printf("skeleton index: %u\n", *m->mh->skeleton_index); }
        if(m->mh->datp_index) { printf("DATP index: %u\n", *m->mh->datp_index); }
        if(m->mh->unknown16) { printf("unknown: %u\n", *m->mh->unknown16); }
        if(m->mh->guide_index) { printf("guide index: %u\n", *m->mh->guide_index); }
        if(m->mh->unknown17) { printf("unknown: %u\n", *m->mh->unknown17); }
        if(m->mh->unknown18) { printf("unknown: %u\n", *m->mh->unknown18); }
        if(m->mh->unknown19) { printf("unknown: %u\n", *m->mh->unknown19); }
        if(m->mh->unknown20) { printf("unknown: %u\n", *m->mh->unknown20); }
    }
}

/**
 @brief Print all loaded EXTH record tags
 @param[in] m MOBIData structure
 */
void print_exth(const MOBIData *m) {
    if (m->eh == NULL) {
        return;
    }
    /* Linked list of MOBIExthHeader structures holds EXTH records */
    const MOBIExthHeader *curr = m->eh;
    if (curr != NULL) {
        printf("\nEXTH records:\n");
    }
    uint32_t val32;
    while (curr != NULL) {
        /* check if it is a known tag and get some more info if it is */
        MOBIExthMeta tag = mobi_get_exthtagmeta_by_tag(curr->tag);
        if (tag.tag == 0) {
            /* unknown tag */
            /* try to print the record both as string and numeric value */
            char *str = _ALLOCA(curr->size + 1);
            unsigned i = 0;
            unsigned char *p = curr->data;
            while (i < curr->size && isprint(*p)) {
                str[i] = (char)*p++;
                i++;
            }
            str[i] = '\0';
            val32 = mobi_decode_exthvalue(curr->data, curr->size);
            printf("Unknown (%i): %s (%u)\n", curr->tag, str, val32);
        } else {
            /* known tag */
            unsigned i = 0;
            size_t size = curr->size;
            char *str = _ALLOCA(2 * size + 1);
            unsigned char *data = curr->data;
            switch (tag.type) {
                /* numeric */
                case EXTH_NUMERIC:
                    val32 = mobi_decode_exthvalue(data, size);
                    printf("%s: %u\n", tag.name, val32);
                    break;
                /* string */
                case EXTH_STRING:
                {
                    char *exth_string = mobi_decode_exthstring(m, data, size);
                    if (exth_string) {
                        printf("%s: %s\n", tag.name, exth_string);
                        free(exth_string);
                    }
                    break;
                }
                /* binary */
                case EXTH_BINARY:
                    while (size--) {
                        uint8_t val8 = *data++;
                        sprintf(&str[i], "%02x", val8);
                        i += 2;
                    }
                    printf("%s: 0x%s\n", tag.name, str);
                    break;
                default:
                    break;
            }

        }
        curr = curr->next;
    }
}

/**
 @brief Print meta data of each document record
 @param[in] m MOBIData structure
 */
void print_records_meta(const MOBIData *m) {
    /* Linked list of MOBIPdbRecord structures holds records data and metadata */
    const MOBIPdbRecord *currec = m->rec;
    while (currec != NULL) {
        printf("offset: %u\n", currec->offset);
        printf("size: %zu\n", currec->size);
        printf("attributes: %hhu\n", currec->attributes);
        printf("uid: %u\n", currec->uid);
        printf("\n");
        currec = currec->next;
    }
}

/**
 @brief Dump each document record to a file into created folder
 @param[in] m MOBIData structure
 @param[in] fullpath File path will be parsed to build basenames of dumped records
 */
int dump_records(const MOBIData *m, const char *fullpath) {
    char dirname[FILENAME_MAX];
    char basename[FILENAME_MAX];
    split_fullpath(fullpath, dirname, basename);
    char newdir[FILENAME_MAX];
    if (outdir_opt) {
        sprintf(newdir, "%s%s_records", outdir, basename);
    } else {
        sprintf(newdir, "%s%s_records", dirname, basename);
    }
    printf("Saving records to %s\n", newdir);
    errno = 0;
    if (mt_mkdir(newdir) != 0 && errno != EEXIST) {
        int errsv = errno;
        printf("Creating directory failed (%s)\n", strerror(errsv));
        return ERROR;
    }
    /* Linked list of MOBIPdbRecord structures holds records data and metadata */
    const MOBIPdbRecord *currec = m->rec;
    int i = 0;
    while (currec != NULL) {
        char name[FILENAME_MAX];
        sprintf(name, "%s%crecord_%i_uid_%i", newdir, separator, i++, currec->uid);
        errno = 0;
        FILE *file = fopen(name, "wb");
        if (file == NULL) {
            int errsv = errno;
            printf("Could not open file for writing: %s (%s)\n", name, strerror(errsv));
            return ERROR;
        }
        fwrite(currec->data, 1, currec->size, file);
        fclose(file);
        currec = currec->next;
    }
    return SUCCESS;
}

/**
 @brief Dump all text records, decompressed and concatenated, to a single rawml file
 @param[in] m MOBIData structure
 @param[in] fullpath File path will be parsed to create a new name for saved file
 */
int dump_rawml(const MOBIData *m, const char *fullpath) {
    char dirname[FILENAME_MAX];
    char basename[FILENAME_MAX];
    split_fullpath(fullpath, dirname, basename);
    char newdir[FILENAME_MAX];
    if (outdir_opt) {
        sprintf(newdir, "%s%s.rawml", outdir, basename);
    } else {
        sprintf(newdir, "%s%s.rawml", dirname, basename);
    }
    printf("Saving rawml to %s\n", newdir);
    errno = 0;
    FILE *file = fopen(newdir, "wb");
    if (file == NULL) {
        int errsv = errno;
        printf("Could not open file for writing: %s (%s)\n", newdir, strerror(errsv));
        return ERROR;
    }
    const MOBI_RET mobi_ret = mobi_dump_rawml(m, file);
    fclose(file);
    if (mobi_ret != MOBI_SUCCESS) {
        printf("Dumping rawml file failed (%i)", mobi_ret);
        return ERROR;
    }
    return SUCCESS;
}

/**
 @brief Dump parsed markup files and resources into created folder
 @param[in] rawml MOBIRawml structure holding parsed records
 @param[in] fullpath File path will be parsed to build basenames of dumped records
 */
int dump_rawml_parts(const MOBIRawml *rawml, const char *fullpath) {
    if (rawml == NULL) {
        printf("Rawml structure not initialized\n");
        return ERROR;
    }
    char dirname[FILENAME_MAX];
    char basename[FILENAME_MAX];
    split_fullpath(fullpath, dirname, basename);
    char newdir[FILENAME_MAX];
    if (outdir_opt) {
        sprintf(newdir, "%s%s_markup", outdir, basename);
    } else {
        sprintf(newdir, "%s%s_markup", dirname, basename);
    }
    printf("Saving markup to %s\n", newdir);
    errno = 0;
    if (mt_mkdir(newdir) != 0 && errno != EEXIST) {
        int errsv = errno;
        printf("Creating directory failed (%s)\n", strerror(errsv));
        return ERROR;
    }
    char partname[FILENAME_MAX];
    if (rawml->markup != NULL) {
        /* Linked list of MOBIPart structures in rawml->markup holds main text files */
        MOBIPart *curr = rawml->markup;
        while (curr != NULL) {
            MOBIFileMeta file_meta = mobi_get_filemeta_by_type(curr->type);
            sprintf(partname, "%s%cpart%05zu.%s", newdir, separator, curr->uid, file_meta.extension);
            errno = 0;
            FILE *file = fopen(partname, "wb");
            if (file == NULL) {
                int errsv = errno;
                printf("Could not open file for writing: %s (%s)\n", partname, strerror(errsv));
                return ERROR;
            }
            printf("part%05zu.%s\n", curr->uid, file_meta.extension);
            errno = 0;
            fwrite(curr->data, 1, curr->size, file);
            if (ferror(file)) {
                int errsv = errno;
                printf("Error writing: %s (%s)\n", partname, strerror(errsv));
                fclose(file);
                return ERROR;
            }
            fclose(file);
            curr = curr->next;
        }
    }
    if (rawml->flow != NULL) {
        /* Linked list of MOBIPart structures in rawml->flow holds supplementary text files */
        MOBIPart *curr = rawml->flow;
        /* skip raw html file */
        curr = curr->next;
        while (curr != NULL) {
            MOBIFileMeta file_meta = mobi_get_filemeta_by_type(curr->type);
            sprintf(partname, "%s%cflow%05zu.%s", newdir, separator, curr->uid, file_meta.extension);
            errno = 0;
            FILE *file = fopen(partname, "wb");
            if (file == NULL) {
                int errsv = errno;
                printf("Could not open file for writing: %s (%s)\n", partname, strerror(errsv));
                return ERROR;
            }
            printf("flow%05zu.%s\n", curr->uid, file_meta.extension);
            errno = 0;
            fwrite(curr->data, 1, curr->size, file);
            if (ferror(file)) {
                int errsv = errno;
                printf("Error writing: %s (%s)\n", partname, strerror(errsv));
                fclose(file);
                return ERROR;
            }
            fclose(file);
            curr = curr->next;
        }
    }
    if (rawml->resources != NULL) {
        /* Linked list of MOBIPart structures in rawml->resources holds binary files */
        MOBIPart *curr = rawml->resources;
        /* jpg, gif, png, bmp, font, audio, video */
        while (curr != NULL) {
            MOBIFileMeta file_meta = mobi_get_filemeta_by_type(curr->type);
            if (curr->size > 0) {
				if (file_meta.type == T_NCX)
					sprintf(partname, "%s%ctoc.%s", newdir, separator, file_meta.extension);
				else if (file_meta.type == T_OPF) {
					sprintf(partname, "%s%ccontent.%s", newdir, separator, file_meta.extension);
				}
				else
					sprintf(partname, "%s%cresource%05zu.%s", newdir, separator, curr->uid, file_meta.extension);
                errno = 0;
                FILE *file = fopen(partname, "wb");
                if (file == NULL) {
                    int errsv = errno;
                    printf("Could not open file for writing: %s (%s)\n", partname, strerror(errsv));
                    return ERROR;
                }
                printf("resource%05zu.%s\n", curr->uid, file_meta.extension);
                errno = 0;
                fwrite(curr->data, 1, curr->size, file);
                if (ferror(file)) {
                    int errsv = errno;
                    printf("Error writing: %s (%s)\n", partname, strerror(errsv));
                    fclose(file);
                    return ERROR;
                }
                fclose(file);
            }
            curr = curr->next;
        }
    }
    return SUCCESS;
}


/**
 @brief Main routine that calls optional subroutines
 @param[in] fullpath Full file path
 */
int loadfilename(const char *fullpath) {
    MOBI_RET mobi_ret;
    int ret = SUCCESS;
    /* Initialize main MOBIData structure */
    MOBIData *m = mobi_init();
    if (m == NULL) {
        printf("Memory allocation failed\n");
        return ERROR;
    }
    /* By default loader will parse KF8 part of hybrid KF7/KF8 file */
    if (parse_kf7_opt) {
        /* Force it to parse KF7 part */
        mobi_parse_kf7(m);
    }
    errno = 0;
    FILE *file = fopen(fullpath, "rb");
    if (file == NULL) {
        int errsv = errno;
        printf("Error opening file: %s (%s)\n", fullpath, strerror(errsv));
        mobi_free(m);
        return ERROR;
    }
    /* MOBIData structure will be filled with loaded document data and metadata */
    mobi_ret = mobi_load_file(m, file);
    fclose(file);
    /* Try to print basic metadata, even if further loading failed */
    /* In case of some unsupported formats it may still print some useful info */
    print_meta(m);
    if (mobi_ret != MOBI_SUCCESS) {
        printf("Error while loading document (%i)\n", mobi_ret);
        mobi_free(m);
        return ERROR;
    }
    /* Try to print EXTH metadata */
    print_exth(m);
#ifdef USE_ENCRYPTION
    if (setpid_opt) {
        /* Try to set key for decompression */
        if (m->rh && m->rh->encryption_type == 0) {
            printf("\nDocument is not encrypted, ignoring PID\n");
        }
        else if (m->rh && m->rh->encryption_type == 1) {
            printf("\nEncryption type 1, ignoring PID\n");
        }
        else {
            printf("\nVerifying PID... ");
            mobi_ret = mobi_drm_setkey(m, pid);
            if (mobi_ret != MOBI_SUCCESS) {
                printf("failed (%i)\n", mobi_ret);
                mobi_free(m);
                return ERROR;
            }
            printf("ok\n");
        }
    }
#endif
    if (print_rec_meta_opt) {
        printf("\nPrinting records metadata...\n");
        print_records_meta(m);
    }
    if (dump_rec_opt) {
        printf("\nDumping raw records...\n");
        ret = dump_records(m, fullpath);
    }
    if (dump_rawml_opt) {
        printf("\nDumping rawml...\n");
        ret = dump_rawml(m, fullpath);
    } else if (dump_parts_opt) {
        printf("\nReconstructing source resources...\n");
        /* Initialize MOBIRawml structure */
        /* This structure will be filled with parsed records data */
        MOBIRawml *rawml = mobi_init_rawml(m);
        if (rawml == NULL) {
            printf("Memory allocation failed\n");
            mobi_free(m);
            return ERROR;
        }

        /* Parse rawml text and other data held in MOBIData structure into MOBIRawml structure */
        mobi_ret = mobi_parse_rawml(rawml, m);
        if (mobi_ret != MOBI_SUCCESS) {
            printf("Parsing rawml failed (%i)\n", mobi_ret);
            mobi_free(m);
            mobi_free_rawml(rawml);
            return ERROR;
        }
        printf("\ndumping resources...\n");
        /* Save parts to files */
        ret = dump_rawml_parts(rawml, fullpath);
        if (ret != SUCCESS) {
            printf("Dumping parts failed\n");
        }
        /* Free MOBIRawml structure */
        mobi_free_rawml(rawml);
	}
    /* Free MOBIData structure */
    mobi_free(m);
    return ret;
}

/**
 @brief Print usage info
 @param[in] progname Executed program name
 */
void usage(const char *progname) {
    printf("usage: %s [-edmrs" PRINT_RUSAGE_ARG "v7] [-o dir]" PRINT_ENC_USG " filename\n", progname);
    printf("       without arguments prints document metadata and exits\n");
	printf("       -e fn   convert to EPUB under file name fn (other dump/print options ignored)\n");
	printf("       -d      dump rawml text record\n");
    printf("       -m      print records metadata\n");
    printf("       -o dir  save output to dir folder\n");
#ifdef USE_ENCRYPTION
    printf("       -p pid  set pid for decryption\n");
#endif
    printf("       -r      dump raw records\n");
    printf("       -s      dump recreated source files\n");
#ifdef HAVE_SYS_RESOURCE_H
    printf("       -u      show rusage\n");
#endif
    printf("       -v      show version and exit\n");
    printf("       -7      parse KF7 part of hybrid file (by default KF8 part is parsed)\n");
    exit(0);
}

#ifdef _WIN32

static int     opterr = 1,             /* if error message should be printed */
optind = 1,             /* index into parent argv vector */
optopt,                 /* character checked for validity */
optreset;               /* reset getopt */
char  *optarg;          /* argument associated with option */

#define BADCH   (int)'?'
#define BADARG  (int)':'
#define EMSG    ""

/*
* getopt --
*      Parse argc/argv argument vector.
*/
int
getopt(
	int nargc,
	char * const *nargv,
	const char *ostr)
{
	const char *__progname = "mobitool";
	static char *place = EMSG;              /* option letter processing */
	char *oli;                              /* option letter list index */

	if (optreset || !*place) {              /* update scanning pointer */
		optreset = 0;
		if (optind >= nargc || *(place = nargv[optind]) != '-') {
			place = EMSG;
			return (EOF);
		}
		if (place[1] && *++place == '-') {      /* found "--" */
			++optind;
			place = EMSG;
			return (EOF);
		}
	}                                       /* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' ||
		!(oli = strchr(ostr, optopt))) {
		/*
		* if the user didn't specify '-' as an option,
		* assume it means EOF.
		*/
		if (optopt == (int)'-')
			return (EOF);
		if (!*place)
			++optind;
		if (opterr && *ostr != ':')
			(void)fprintf(stderr,
				"%s: illegal option -- %c\n", __progname, optopt);
		return (BADCH);
	}
	if (*++oli != ':') {                    /* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	}
	else {                                  /* need an argument */
		if (*place)                     /* no white space */
			optarg = place;
		else if (nargc <= ++optind) {   /* no arg */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (opterr)
				(void)fprintf(stderr,
					"%s: option requires an argument -- %c\n",
					__progname, optopt);
			return (BADCH);
		}
		else                            /* white space */
			optarg = nargv[optind];
		place = EMSG;
		++optind;
	}
	return (optopt);                        /* dump back option letter */
}
#endif

/**
 @brief Main
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
    }
    int opterr = 0;
    int c;
    while((c = getopt(argc, argv, "e:dmo:" PRINT_ENC_ARG "rs" PRINT_RUSAGE_ARG "v7")) != -1)
        switch(c) {
            case 'd':
                dump_rawml_opt = 1;
                break;
            case 'm':
                print_rec_meta_opt = 1;
                break;
            case 'o':
                outdir_opt = 1;
                size_t outdir_length = strlen(optarg);
                if (outdir_length >= FILENAME_MAX - 1) {
                    printf("Output directory name too long\n");
                    return ERROR;
                }
                strncpy(outdir, optarg, FILENAME_MAX - 1);
                if (!dir_exists(outdir)) {
                    printf("Output directory is not valid\n");
                    return ERROR;
                }
                if (optarg[outdir_length - 1] != separator) {
                    // append separator
                    if (outdir_length >= FILENAME_MAX - 2) {
                        printf("Output directory name too long\n");
                        return ERROR;
                    }
                    outdir[outdir_length++] = separator;
                    outdir[outdir_length] = '\0';
                }
                break;
#ifdef USE_ENCRYPTION
            case 'p':
                setpid_opt = 1;
                pid = optarg;
                break;
#endif
            case 'r':
                dump_rec_opt = 1;
                break;
            case 's':
                dump_parts_opt = 1;
                break;
			case 'e':
				dump_epub_opt = 1;
				size_t fn_length = strlen(optarg);
				if (fn_length >= FILENAME_MAX - 1) {
					printf("EPUB file name too long\n");
					return ERROR;
				}
				strncpy(epub_fn, optarg, FILENAME_MAX - 1);
				break;
#ifdef HAVE_SYS_RESOURCE_H
            case 'u':
                print_rusage_opt = 1;
                break;
#endif
            case 'v':
                printf("mobitool build: " __DATE__ " " __TIME__ " (" COMPILER ")\n");
                printf("libmobi: %s\n", mobi_version());
                return 0;
            case '7':
                parse_kf7_opt = 1;
                break;
            case '?':
#ifdef USE_ENCRYPTION
                if (optopt == 'p') {
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                }
                else
#endif
                if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'\n", optopt);
                }
                else {
                    fprintf(stderr, "Unknown option character `\\x%x'\n", optopt);
                }
                usage(argv[0]);
            default:
                usage(argv[0]);
        }
    if (argc <= optind) {
        printf("Missing filename\n");
        usage(argv[0]);
    }
    int ret = 0;
    char filename[FILENAME_MAX];
    strncpy(filename, argv[optind], FILENAME_MAX - 1);
	
	if (dump_epub_opt) {
		ret = convertMobiToEpub(filename, epub_fn, pid, parse_kf7_opt == 1) ? 1 : 0;
	}
	else {
		ret = loadfilename(filename);
	}
#ifdef HAVE_SYS_RESOURCE_H
    if (print_rusage_opt) {
        /* rusage */
        struct rusage ru;
        struct timeval utime;
        struct timeval stime;
        getrusage(RUSAGE_SELF, &ru);
        utime = ru.ru_utime;
        stime = ru.ru_stime;
        printf("RUSAGE: ru_utime => %lld.%lld sec.; ru_stime => %lld.%lld sec.\n",
               (long long) utime.tv_sec, (long long) utime.tv_usec,
               (long long) stime.tv_sec, (long long) stime.tv_usec);
    }
#endif
    return ret;
}
