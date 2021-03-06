/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
       * Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
         copyright notice, this list of conditions and the following
         disclaimer in the documentation and/or other materials provided
         with the distribution.
       * Neither the name of The Linux Foundation nor the names of its
         contributors may be used to endorse or promote products derived
         from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>

#define QCDT_MAGIC     "QCDT"  /* Master DTB magic */
#define QCDT_VERSION   1       /* QCDT version */
#define QCDT_VERSION_NEW   2       /* QCDT version */

#define QCDT_DT_TAG    "qcom,msm-id = <"
#define QCDT_BOARD_TAG "qcom,board-id = <"

#define PAGE_SIZE_DEF  4096
#define PAGE_SIZE_MAX  (1024*1024)

#define log_err(x...)  printf(x)
#define log_info(x...) printf(x)
#define log_dbg(x...)  { if (verbose) printf(x); }

#define COPY_BLK       1024    /* File copy block size */

#define RC_SUCCESS     0
#define RC_ERROR       -1

struct chipInfo_t {
  uint32_t chipset;
  uint32_t platform;
  uint32_t subtype;
  uint32_t revNum;
  uint32_t dtb_size;
  char     *dtb_file;
  struct chipInfo_t *prev;
  struct chipInfo_t *next;
  struct chipInfo_t *master;
  int      wroteDtb;
  uint32_t master_offset;
  struct chipInfo_t *t_next;
};

struct chipInfo_t *chip_list;

struct chipId_t {
  uint32_t chipset;
  uint32_t revNum;
  struct chipId_t *next;
  struct chipId_t *t_next;
};

struct chipSt_t {
  uint32_t platform;
  uint32_t subtype;
  struct chipSt_t *next;
  struct chipSt_t *t_next;
};

char *input_dir;
char *output_file;
char *dtc_path;
char *dt_tag = QCDT_DT_TAG;
int   verbose;
int   page_size = PAGE_SIZE_DEF;
int   force_v2;


void print_help()
{
    log_info("dtbTool version %d (kinda :) )\n", QCDT_VERSION);
    log_info("dtbTool [options] -o <output file> <input DTB path>\n");
    log_info("  options:\n");
    log_info("  --output-file/-o     output file\n");
    log_info("  --dtc-path/-p        path to dtc\n");
    log_info("  --page-size/-s       page size in bytes (default = 4096)\n");
    log_info("  --dt-tag/-d          alternate QCDT_DT_TAG\n");
    log_info("  --verbose/-v         verbose\n");
    log_info("  --force-v2/-2        use dtb v2 format\n");
    log_info("  --help/-h            this help screen\n");
}

int parse_commandline(int argc, char *const argv[])
{
    int c;

    struct option long_options[] = {
        {"output-file", 1, 0, 'o'},
        {"dtc-path",    1, 0, 'p'},
        {"page-size",   1, 0, 's'},
        {"dt-tag",      1, 0, 'd'},
        {"verbose",     0, 0, 'v'},
        {"help",        0, 0, 'h'},
        {"force-v2",    0, 0, '2'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "-o:p:s:d:vh2", long_options, NULL))
           != -1) {
        switch (c) {
        case 1:
            if (!input_dir)
                input_dir = optarg;
            break;
        case 'o':
            output_file = optarg;
            break;
        case 'p':
            dtc_path = optarg;
            break;
        case 's':
            page_size = atoi(optarg);
            if ((page_size <= 0) || (page_size > (PAGE_SIZE_MAX))) {
                log_err("Invalid page size (> 0 and <=1MB\n");
                return RC_ERROR;
            }
            break;
        case 'd':
            dt_tag = optarg;
            break;
        case 'v':
            verbose = 1;
            break;
        case '2':
            force_v2 = 1;
            break;
        case 'h':
        default:
            return RC_ERROR;
        }
    }

    if (!output_file) {
        log_err("Output file must be specified\n");
        return RC_ERROR;
    }

    if (!input_dir)
        input_dir = "./";

    if (!dtc_path)
        dtc_path = "";

    return RC_SUCCESS;
}

/* Unique entry sorted list add (by chipset->platform->rev) */
int chip_add(struct chipInfo_t *c)
{
    struct chipInfo_t *x = chip_list;

    if (!chip_list) {
        chip_list = c;
        c->next = NULL;
        c->prev = NULL;
        return RC_SUCCESS;
    }

    while (1) {
        if ((c->chipset < x->chipset) ||
            ((c->chipset == x->chipset) &&
             ((c->platform < x->platform) ||
              ((c->platform == x->platform) &&
               ((c->subtype < x->subtype) ||
                ((c->subtype == x->subtype) &&
                 (c->revNum < x->revNum))))))) {
            if (!x->prev) {
                c->next = x;
                c->prev = NULL;
                x->prev = c;
                chip_list = c;
                break;
            } else {
                c->next = x;
                c->prev = x->prev;
                x->prev->next = c;
                x->prev = c;
                break;
            }
        }
        if ((c->chipset == x->chipset) &&
            (c->platform == x->platform) &&
            (c->subtype == x->subtype) &&
            (c->revNum == x->revNum)) {
            return RC_ERROR;  /* duplicate */
        }
        if (!x->next) {
            c->prev = x;
            c->next = NULL;
            x->next = c;
            break;
        }
        x = x->next;
    }
    return RC_SUCCESS;
}

void chip_deleteall()
{
    struct chipInfo_t *c = chip_list, *t;

    while (c) {
        t = c;
        c = c->next;
        if (t->dtb_file)
            free(t->dtb_file);
        free(t);
    }
}

/*
  For v1 Extract 'qcom,msm-id' parameter triplet from DTB
      qcom,msm-id = <x y z>;

  For v2 Extract 'qcom,msm-id', 'qcom,board-id' parameter double from DTB
      qcom,msm-id = <x z> i.e chipset, revision number;
      qcom,board-id = <y y'> i.e platform and sub-type;
 */

struct chipInfo_t *getChipInfo(const char *filename, int *num, uint32_t msmversion)
{

    const char str1[] = "dtc -I dtb -O dts \"";
    const char str2[] = "\" 2>&1";
    char *buf, *pos;
    char *line = NULL;
    size_t line_size;
    FILE *pfile;
    int llen;
    struct chipInfo_t *chip = NULL, *tmp;
    uint32_t data[3] = {0, 0, 0};
    uint32_t data_st[2] = {0, 0};
    char *tok, *sptr = NULL;
    int i, entryValid, entryEnded;
    int count = 0, count1 = 0, count2 =0;
    int entryValidST, entryEndedST, entryValidDT, entryEndedDT;
    struct chipId_t *chipId = NULL, *cId = NULL, *tmp_id = NULL;
    struct chipSt_t *chipSt = NULL, *cSt = NULL, *tmp_st = NULL;

    line_size = 1024;
    line = (char *)malloc(line_size);
    if (!line) {
        log_err("Out of memory\n");
        return NULL;
    }

    llen = sizeof(char) * (strlen(dtc_path) +
                           strlen(str1) +
                           strlen(str2) +
                           strlen(filename) + 1);
    buf = (char *)malloc(llen);
    if (!buf) {
        log_err("Out of memory\n");
        free(line);
        return NULL;
    }

    strncpy(buf, dtc_path, llen);
    strncat(buf, str1, llen);
    strncat(buf, filename, llen);
    strncat(buf, str2, llen);

    pfile = popen(buf, "r");
    free(buf);

    if (pfile == NULL) {
        log_err("... skip, fail to decompile dtb\n");
    } else {
        /* Find "qcom,msm-id" */
        while ((llen = getline(&line, &line_size, pfile)) != -1) {
            if (msmversion == 1) {
                if ((pos = strstr(line, dt_tag)) != NULL) {
                    pos += strlen(dt_tag);

                    entryEnded = 0;
                    while (1) {
                        entryValid = 1;
                        for (i = 0; i < 3; i++) {
                            tok = strtok_r(pos, " \t", &sptr);
                            pos = NULL;
                            if (tok != NULL) {
                                if (*tok == '>') {
                                    entryEnded = 1;
                                    entryValid = 0;
                                    break;
                                }
                                data[i] = strtoul(tok, NULL, 0);
                            } else {
                                data[i] = 0;
                                entryValid = 0;
                                entryEnded = 1;
                            }
                        }
                        if (entryEnded) {
                            free(line);
                            pclose(pfile);
                            *num = count;
                            return chip;
                        }
                        if (entryValid) {
                            tmp = (struct chipInfo_t *)
                                      malloc(sizeof(struct chipInfo_t));
                            if (!tmp) {
                                log_err("Out of memory\n");
                                break;
                            }
                            if (!chip) {
                                chip = tmp;
                                chip->t_next = NULL;
                            } else {
                                tmp->t_next = chip->t_next;
                                chip->t_next = tmp;
                            }
                            tmp->chipset  = data[0];
                            tmp->platform = data[1];
                            tmp->subtype  = 0;
                            tmp->revNum   = data[2];
                            tmp->dtb_size = 0;
                            tmp->dtb_file = NULL;
                            tmp->master   = chip;
                            tmp->wroteDtb = 0;
                            tmp->master_offset = 0;
                            count++;
                        }
                    }

                    log_err("... skip, incorrect '%s' format\n", dt_tag);
                    break;
                }
            } else if (msmversion == 2) {
                if ((pos = strstr(line, dt_tag)) != NULL) {
                    pos += strlen(dt_tag);

                    entryEndedDT = 0;
                    for (;entryEndedDT < 1;) {
                        entryValidDT = 1;
                        for (i = 0; i < 2; i++) {
                            tok = strtok_r(pos, " \t", &sptr);
                            pos = NULL;
                            if (tok != NULL) {
                                if (*tok == '>') {
                                    entryEndedDT = 1;
                                    entryValidDT = 0;
                                    break;
                                }
                                data_st[i] = strtoul(tok, NULL, 0);
                            } else {
                                data_st[i] = 0;
                                entryValidDT = 0;
                                entryEndedDT = 1;
                            }
                        }

                        if (entryValidDT) {
                            tmp_id = (struct chipId_t *)
                                         malloc(sizeof(struct chipId_t));
                            if (!tmp_id) {
                                log_err("Out of memory\n");
                                break;
                            }
                            if (!chipId) {
                                chipId = tmp_id;
                                cId = tmp_id;
                                chipId->t_next = NULL;
                            } else {
                                tmp_id->t_next = chipId->t_next;
                                chipId->t_next = tmp_id;
                            }
                            tmp_id->chipset = data_st[0];
                            tmp_id->revNum= data_st[1];
                            count1++;
                        }
                    }
                }

                if ((pos = strstr(line,QCDT_BOARD_TAG)) != NULL) {
                    pos += strlen(QCDT_BOARD_TAG);
                    entryEndedST = 0;
                    for (;entryEndedST < 1;) {
                        entryValidST = 1;
                        for (i = 0; i < 2; i++) {
                            tok = strtok_r(pos, " \t", &sptr);
                            pos = NULL;
                            if (tok != NULL) {
                                if (*tok == '>') {
                                    entryEndedST = 1;
                                    entryValidST = 0;
                                    break;
                                }
                                data_st[i] = strtoul(tok, NULL, 0);
                            } else {
                                data_st[i] = 0;
                                entryValidST = 0;
                                entryEndedST = 1;
                            }
                        }
                        if (entryValidST) {
                            tmp_st = (struct chipSt_t *)
                                       malloc(sizeof(struct chipSt_t));
                            if (!tmp_st) {
                                log_err("Out of memory\n");
                                break;
                            }

                            if (!chipSt) {
                                chipSt = tmp_st;
                                cSt = tmp_st;
                                chipSt->t_next = NULL;
                            } else {
                                tmp_st->t_next = chipSt->t_next;
                                chipSt->t_next = tmp_st;
                            }

                            tmp_st->platform = data_st[0];
                            tmp_st->subtype= data_st[1];
                            count2++;
                        }
                    }
                }
            }
        }
    }

    if (line)
        free(line);

    if (force_v2 || msmversion == 2) {

    if (count1 == 0) {
        log_err("... skip, incorrect '%s' format\n", dt_tag);
        return NULL;
    }
    if (count2 == 0) {
        log_err("... skip, incorrect '%s' format\n", QCDT_BOARD_TAG);
        return NULL;
    }

    tmp_st = cSt;
    while (cId != NULL) {
        while (cSt != NULL) {
            tmp = (struct chipInfo_t *)
                      malloc(sizeof(struct chipInfo_t));
            if (!tmp) {
                log_err("Out of memory\n");
                break;
            }
            if (!chip) {
                chip = tmp;
                chip->t_next = NULL;
            } else {
                tmp->t_next = chip->t_next;
                chip->t_next = tmp;
            }

            tmp->chipset  = cId->chipset;
            tmp->platform = cSt->platform;
            tmp->revNum   = cId->revNum;
            tmp->subtype  = cSt->subtype;
            tmp->dtb_size = 0;
            tmp->dtb_file = NULL;
            tmp->master   = chip;
            tmp->wroteDtb = 0;
            tmp->master_offset = 0;

            cSt = cSt->t_next;

        }
        cSt = tmp_st;
        cId = cId->t_next;
    }

    if (entryEndedST  == 1 && entryEndedDT == 1) {
        pclose(pfile);
        *num = count1;
        free(chipSt);
        free(chipId);
        return chip;
    }

    } else {
        pclose(pfile);
    }

    return NULL;
}

/* Get the version-id based on dtb files */
int GetVersionInfo(const char *filename)
{
    const char str1[] = "dtc -I dtb -O dts \"";
    const char str2[] = "\" 2>&1";
    char *buf, *pos;
    char *line = NULL;
    size_t line_size;
    FILE *pfile;
    int llen;
    int v = 1;

    line_size = 1024;
    line = (char *)malloc(line_size);
    if (!line) {
        log_err("Out of memory\n");
        return 0;
    }

    llen = sizeof(char) * (strlen(dtc_path) +
                           strlen(str1) +
                           strlen(str2) +
                           strlen(filename) + 1);
    buf = (char *)malloc(llen);
    if (!buf) {
        log_err("Out of memory\n");
        free(line);
        return 0;
    }

    strncpy(buf, dtc_path, llen);
    strncat(buf, str1, llen);
    strncat(buf, filename, llen);
    strncat(buf, str2, llen);

    pfile = popen(buf, "r");
    free(buf);

    if (pfile == NULL) {
        log_err("... skip, fail to decompile dtb\n");
    } else {
        /* Find the type of version */
        while ((llen = getline(&line, &line_size, pfile)) != -1) {
            if ((pos = strstr(line,QCDT_BOARD_TAG)) != NULL) {
                v = 2;
                break;
            }
        }
    }
    if (v == 1)
        log_info(" Old Version:%d\n", v);
    free(line);

    return v;
}

/* Extract 'qcom,msm-id' 'qcom,board-id' parameter from DTB
   v1 format:
      qcom,msm-id = <x y z> [, <x2 y2 z2> ...];
   v2 format:
      qcom,msm-id = <x z> [, <x2 z2> ...;
      qcom,board-id = <y y'> [, <y2 y2'> ...;
   Fields:
      x  = chipset
      y  = platform
      y' = subtype
      z  = soc rev
 */
int main(int argc, char **argv)
{
    char buf[COPY_BLK];
    struct chipInfo_t *chip, *t_chip;
    struct dirent *dp;
    FILE *pInputFile;
    char *filename;
    int padding;
    uint8_t *filler = NULL;
    int numBytesRead = 0;
    int totBytesRead = 0;
    int out_fd;
    int flen;
    int rc = RC_SUCCESS;
    int dtb_count = 0, dtb_offset = 0;
    size_t wrote = 0, expected = 0;
    struct stat st;
    uint32_t version = QCDT_VERSION;
    int num;
    uint32_t dtb_size;
    int msmversion = 0;

    log_info("DTB combiner:\n");

    if (parse_commandline(argc, argv) != RC_SUCCESS) {
        print_help();
        return RC_ERROR;
    }

    log_info("  Input directory: '%s'\n", input_dir);
    log_info("  Output file: '%s'\n", output_file);

    DIR *dir = opendir(input_dir);
    if (!dir) {
        log_err("Failed to open input directory '%s'\n", input_dir);
        return RC_ERROR;
    }

    filler = (uint8_t *)malloc(page_size);
    if (!filler) {
        log_err("Out of memory\n");
        closedir(dir);
        return RC_ERROR;
    }
    memset(filler, 0, page_size);

    /* Open the .dtb files in the specified path, decompile and
       extract "qcom,msm-id" parameter
     */
    while ((dp = readdir(dir)) != NULL) {

        flen = strlen(input_dir) + strlen(dp->d_name) + 1;
        filename = (char *)malloc(flen);
        if (!filename) {
            log_err("Out of memory\n");
            rc = RC_ERROR;
            break;
        }
        strncpy(filename, input_dir, flen);
        strncat(filename, dp->d_name, flen);

        if (stat(filename, &st) != 0 || !S_ISREG(st.st_mode)) {
            free(filename);
            continue;
        }

        flen = strlen(dp->d_name);
        if ((flen <= 4) || (strncmp(&dp->d_name[flen-4], ".dtb", 4) != 0)) {
            free(filename);
            continue;
        }

        log_info("Found file: %s ... ", dp->d_name);

        /* To identify the version number */
        msmversion = force_v2 ? GetVersionInfo(filename) : 1;

        num = 1;
        chip = getChipInfo(filename, &num, msmversion);

        if (msmversion == 1) {
            if (!chip) {
                log_err("skip, failed to scan for '%s' tag\n",
                        dt_tag);
                free(filename);
                continue;
            }
        }
        if (msmversion == 2) {
            if (!chip) {
                log_err("skip, failed to scan for '%s' or '%s' tag\n",
                        dt_tag, QCDT_BOARD_TAG);
                free(filename);
                continue;
            }
        }

        if (st.st_size == 0) {
            log_err("skip, failed to get DTB size\n");
            free(filename);
            continue;
        }

        log_info("chipset: %u, rev: %u, platform: %u, subtype: %u\n",
                 chip->chipset, chip->revNum, chip->platform, chip->subtype);

        for (t_chip = chip->t_next; t_chip; t_chip = t_chip->t_next) {
            log_info("   additional chipset: %u, rev: %u, platform: %u, subtype: %u\n",
                     t_chip->chipset, t_chip->revNum, t_chip->platform, t_chip->subtype);
        }

        rc = chip_add(chip);
        if (rc != RC_SUCCESS) {
            log_err("... duplicate info, skipped\n");
            free(filename);
            continue;
        }

        dtb_count++;

        chip->dtb_size = st.st_size +
                           (page_size - (st.st_size % page_size));
        chip->dtb_file = filename;

        for (t_chip = chip->t_next; t_chip; t_chip = t_chip->t_next) {
            rc = chip_add(t_chip);
            if (rc != RC_SUCCESS) {
                log_err("... duplicate info, skipped (chipset %u, rev: %u, platform: %u, subtype %u:\n",
                     t_chip->chipset, t_chip->revNum, t_chip->platform, t_chip->subtype);
                continue;
            }
            dtb_count++;
        }
    }
    closedir(dir);
    log_info("=> Found %d unique DTB(s)\n", dtb_count);

    if (!dtb_count)
        goto cleanup;


    /* Generate the master DTB file:

       Simplify write error handling by just checking for actual vs
       expected bytes written at the end.
     */

    log_info("\nGenerating master DTB... ");

    out_fd = open(output_file, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
    if (!out_fd < 0) {
        log_err("Cannot create '%s'\n", output_file);
        rc = RC_ERROR;
        goto cleanup;
    }

    if (force_v2) {
        version = QCDT_VERSION_NEW;
    }

    /* Write header info */
    wrote += write(out_fd, QCDT_MAGIC, sizeof(uint8_t) * 4); /* magic */
    wrote += write(out_fd, &version, sizeof(uint32_t));      /* version */
    wrote += write(out_fd, (uint32_t *)&dtb_count, sizeof(uint32_t));
                                                             /* #DTB */

    /* Calculate offset of first DTB block */
    dtb_offset = 12               + /* header */
                 ((force_v2 ? 24 : 20) * dtb_count) + /* DTB table entries */
                 4;                 /* end of table indicator */
    /* Round up to page size */
    padding = page_size - (dtb_offset % page_size);
    dtb_offset += padding;
    expected = dtb_offset;

    /* Write index table:
         chipset
         platform
         subtype
         soc rev
         dtb offset
         dtb size
     */
    for (chip = chip_list; chip; chip = chip->next) {
        wrote += write(out_fd, &chip->chipset, sizeof(uint32_t));
        wrote += write(out_fd, &chip->platform, sizeof(uint32_t));
        if (force_v2)
            wrote += write(out_fd, &chip->subtype, sizeof(uint32_t));
        wrote += write(out_fd, &chip->revNum, sizeof(uint32_t));
        if (chip->master->master_offset != 0) {
            wrote += write(out_fd, &chip->master->master_offset, sizeof(uint32_t));
        } else {
            wrote += write(out_fd, &expected, sizeof(uint32_t));
            chip->master->master_offset = expected;
            expected += chip->master->dtb_size;
        }
        wrote += write(out_fd, &chip->master->dtb_size, sizeof(uint32_t));
    }

    rc = RC_SUCCESS;
    wrote += write(out_fd, &rc, sizeof(uint32_t)); /* end of table indicator */
    if (padding > 0)
        wrote += write(out_fd, filler, padding);

    /* Write DTB's */
    for (chip = chip_list; chip; chip = chip->next) {
        if (chip->master->wroteDtb) {
            continue;
        }

        chip->master->wroteDtb = 1;
        filename = chip->master->dtb_file;
        dtb_size = chip->master->dtb_size;

        log_dbg("\n (writing '%s' - %u bytes) ", filename, dtb_size);
        pInputFile = fopen(filename, "r");
        if (pInputFile != NULL) {
            totBytesRead = 0;
            while ((numBytesRead = fread(buf, 1, COPY_BLK, pInputFile)) > 0) {
                wrote += write(out_fd, buf, numBytesRead);
                totBytesRead += numBytesRead;
            }
            fclose(pInputFile);
            padding = page_size - (totBytesRead % page_size);
            if ((uint32_t)(totBytesRead + padding) != dtb_size) {
                log_err("DTB size mismatch, please re-run: expected %d vs actual %d (%s)\n",
                        dtb_size, totBytesRead + padding,
                        filename);
                rc = RC_ERROR;
                break;
            }
            if (padding > 0)
                wrote += write(out_fd, filler, padding);
        } else {
            log_err("failed to open DTB '%s'\n", filename);
            rc = RC_ERROR;
            break;
        }
    }
    close(out_fd);

    if (expected != wrote) {
        log_err("error writing output file, please rerun: size mismatch %zu vs %zu\n",
                expected, wrote);
        rc = RC_ERROR;
    } else
        log_dbg("Total wrote %zu bytes\n", wrote);

    if (rc != RC_SUCCESS)
        unlink(output_file);
    else
        log_info("completed\n");

cleanup:
    free(filler);
    chip_deleteall();
    return rc;
}
