/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2002-2012 Free Software Foundation Europe e.V.
   Copyright (C) 2011-2012 Planets Communications B.V.
   Copyright (C) 2013-2013 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/
/*
 * Parse Bootstrap Records (used for restores)
 *
 * Kern Sibbald, June MMII
 */

#include "include/bareos.h"
#include "jcr.h"
#include "bsr.h"

typedef BootStrapRecord *(ITEM_HANDLER)(LEX *lc, BootStrapRecord *bsr);

static BootStrapRecord *store_vol(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_mediatype(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *StoreDevice(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_client(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_job(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_jobid(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_count(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *StoreJobtype(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_joblevel(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_findex(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_sessid(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_volfile(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_volblock(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_voladdr(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_sesstime(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_include(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_exclude(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_stream(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_slot(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_fileregex(LEX *lc, BootStrapRecord *bsr);
static BootStrapRecord *store_nothing(LEX *lc, BootStrapRecord *bsr);

struct kw_items {
  const char *name;
  ITEM_HANDLER *handler;
};

/*
 * List of all keywords permitted in bsr files and their handlers
 */
struct kw_items items[] = {{"volume", store_vol},
                           {"mediatype", store_mediatype},
                           {"client", store_client},
                           {"job", store_job},
                           {"jobid", store_jobid},
                           {"count", store_count},
                           {"fileindex", store_findex},
                           {"jobtype", StoreJobtype},
                           {"joblevel", store_joblevel},
                           {"volsessionid", store_sessid},
                           {"volsessiontime", store_sesstime},
                           {"include", store_include},
                           {"exclude", store_exclude},
                           {"volfile", store_volfile},
                           {"volblock", store_volblock},
                           {"voladdr", store_voladdr},
                           {"stream", store_stream},
                           {"slot", store_slot},
                           {"device", StoreDevice},
                           {"fileregex", store_fileregex},
                           {"storage", store_nothing},
                           {NULL, NULL}};

/*
 * Create a BootStrapRecord record
 */
static BootStrapRecord *new_bsr() {

  BootStrapRecord *bsr = (BootStrapRecord *)malloc(sizeof(BootStrapRecord));
  memset(bsr, 0, sizeof(BootStrapRecord));
  return bsr;
}

/*
 * Format a scanner error message
 */
static void s_err(const char *file, int line, LEX *lc, const char *msg, ...) {

  va_list ap;
  int len, maxlen;
  PoolMem buf(PM_NAME);
  JobControlRecord *jcr = (JobControlRecord *)(lc->caller_ctx);

  while (1) {
    maxlen = buf.size() - 1;
    va_start(ap, msg);
    len = Bvsnprintf(buf.c_str(), maxlen, msg, ap);
    va_end(ap);

    if (len < 0 || len >= (maxlen - 5)) {
      buf.ReallocPm(maxlen + maxlen / 2);
      continue;
    }

    break;
  }

  if (jcr) {
    Jmsg(jcr, M_FATAL, 0,
         _("Bootstrap file error: %s\n"
           "            : Line %d, col %d of file %s\n%s\n"),
         buf.c_str(), lc->line_no, lc->col_no, lc->fname, lc->line);
  } else {
    e_msg(file, line, M_FATAL, 0,
          _("Bootstrap file error: %s\n"
            "            : Line %d, col %d of file %s\n%s\n"),
          buf.c_str(), lc->line_no, lc->col_no, lc->fname, lc->line);
  }
}

/*
 * Format a scanner warning message
 */
static void s_warn(const char *file, int line, LEX *lc, const char *msg, ...) {

  va_list ap;
  int len, maxlen;
  PoolMem buf(PM_NAME);
  JobControlRecord *jcr = (JobControlRecord *)(lc->caller_ctx);

  while (1) {
    maxlen = buf.size() - 1;
    va_start(ap, msg);
    len = Bvsnprintf(buf.c_str(), maxlen, msg, ap);
    va_end(ap);

    if (len < 0 || len >= (maxlen - 5)) {
      buf.ReallocPm(maxlen + maxlen / 2);
      continue;
    }

    break;
  }

  if (jcr) {
    Jmsg(jcr, M_WARNING, 0,
         _("Bootstrap file warning: %s\n"
           "            : Line %d, col %d of file %s\n%s\n"),
         buf.c_str(), lc->line_no, lc->col_no, lc->fname, lc->line);
  } else {
    p_msg(file, line, 0,
          _("Bootstrap file warning: %s\n"
            "            : Line %d, col %d of file %s\n%s\n"),
          buf.c_str(), lc->line_no, lc->col_no, lc->fname, lc->line);
  }
}

static inline bool IsFastRejectionOk(BootStrapRecord *bsr) {




  /*
   * Although, this can be optimized, for the moment, require
   *  all bsrs to have both sesstime and sessid set before
   *  we do fast rejection.
   */
  for (; bsr; bsr = bsr->next) {
    if (!(bsr->sesstime && bsr->sessid)) {
      return false;
    }
  }
  return true;
}

static inline bool IsPositioningOk(BootStrapRecord *bsr) {




  /*
   * Every bsr should have a volfile entry and a volblock entry
   * or a VolAddr
   *   if we are going to use positioning
   */
  for (; bsr; bsr = bsr->next) {
    if (!((bsr->volfile && bsr->volblock) || bsr->voladdr)) {
      return false;
    }
  }
  return true;
}

/*
 * Parse Bootstrap file
 */
BootStrapRecord *parse_bsr(JobControlRecord *jcr, char *fname) {

  LEX *lc = NULL;
  int token, i;
  BootStrapRecord *root_bsr = new_bsr();
  BootStrapRecord *bsr = root_bsr;

  Dmsg1(300, "Enter parse_bsf %s\n", fname);
  if ((lc = lex_open_file(lc, fname, s_err, s_warn)) == NULL) {
    berrno be;
    Emsg2(M_ERROR_TERM, 0, _("Cannot open bootstrap file %s: %s\n"), fname, be.bstrerror());
  }
  lc->caller_ctx = (void *)jcr;
  while ((token = LexGetToken(lc, BCT_ALL)) != BCT_EOF) {
    Dmsg1(300, "parse got token=%s\n", lex_tok_to_str(token));
    if (token == BCT_EOL) {
      continue;
    }
    for (i = 0; items[i].name; i++) {
      if (Bstrcasecmp(items[i].name, lc->str)) {
        token = LexGetToken(lc, BCT_ALL);
        Dmsg1(300, "in BCT_IDENT got token=%s\n", lex_tok_to_str(token));
        if (token != BCT_EQUALS) {
          scan_err1(lc, "expected an equals, got: %s", lc->str);
          bsr = NULL;
          break;
        }
        Dmsg1(300, "calling handler for %s\n", items[i].name);
        /*
         * Call item handler
         */
        bsr = items[i].handler(lc, bsr);
        i = -1;
        break;
      }
    }
    if (i >= 0) {
      Dmsg1(300, "Keyword = %s\n", lc->str);
      scan_err1(lc, "Keyword %s not found", lc->str);
      bsr = NULL;
      break;
    }
    if (!bsr) {
      break;
    }
  }
  lc = lex_close_file(lc);
  Dmsg0(300, "Leave parse_bsf()\n");
  if (!bsr) {
    FreeBsr(root_bsr);
    root_bsr = NULL;
  }
  if (root_bsr) {
    root_bsr->use_fast_rejection = IsFastRejectionOk(root_bsr);
    root_bsr->use_positioning = IsPositioningOk(root_bsr);
  }
  for (bsr = root_bsr; bsr; bsr = bsr->next) {
    bsr->root = root_bsr;
  }
  return root_bsr;
}

static BootStrapRecord *store_vol(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrVolume *volume;
  char *p, *n;

  token = LexGetToken(lc, BCT_STRING);
  if (token == BCT_ERROR) {
    return NULL;
  }
  if (bsr->volume) {
    bsr->next = new_bsr();
    bsr->next->prev = bsr;
    bsr = bsr->next;
  }
  /* This may actually be more than one volume separated by a |
   * If so, separate them.
   */
  for (p = lc->str; p && *p;) {
    n = strchr(p, '|');
    if (n) {
      *n++ = 0;
    }
    volume = (BsrVolume *)malloc(sizeof(BsrVolume));
    memset(volume, 0, sizeof(BsrVolume));
    bstrncpy(volume->VolumeName, p, sizeof(volume->VolumeName));

    /*
     * Add it to the end of the volume chain
     */
    if (!bsr->volume) {
      bsr->volume = volume;
    } else {
      BsrVolume *bc = bsr->volume;
      for (; bc->next; bc = bc->next) {
      }
      bc->next = volume;
    }
    p = n;
  }
  return bsr;
}

/*
 * Shove the MediaType in each Volume in the current bsr\
 */
static BootStrapRecord *store_mediatype(LEX *lc, BootStrapRecord *bsr) {

  int token;

  token = LexGetToken(lc, BCT_STRING);
  if (token == BCT_ERROR) {
    return NULL;
  }
  if (!bsr->volume) {
    Emsg1(M_ERROR, 0, _("MediaType %s in bsr at inappropriate place.\n"), lc->str);
    return bsr;
  }
  BsrVolume *bv;
  for (bv = bsr->volume; bv; bv = bv->next) {
    bstrncpy(bv->MediaType, lc->str, sizeof(bv->MediaType));
  }
  return bsr;
}

static BootStrapRecord *store_nothing(LEX *lc, BootStrapRecord *bsr) {

  int token;

  token = LexGetToken(lc, BCT_STRING);
  if (token == BCT_ERROR) {
    return NULL;
  }
  return bsr;
}

/*
 * Shove the Device name in each Volume in the current bsr
 */
static BootStrapRecord *StoreDevice(LEX *lc, BootStrapRecord *bsr) {



  int token;

  token = LexGetToken(lc, BCT_STRING);
  if (token == BCT_ERROR) {
    return NULL;
  }
  if (!bsr->volume) {
    Emsg1(M_ERROR, 0, _("Device \"%s\" in bsr at inappropriate place.\n"), lc->str);
    return bsr;
  }
  BsrVolume *bv;
  for (bv = bsr->volume; bv; bv = bv->next) {
    bstrncpy(bv->device, lc->str, sizeof(bv->device));
  }
  return bsr;
}

static BootStrapRecord *store_client(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrClient *client;

  for (;;) {
    token = LexGetToken(lc, BCT_NAME);
    if (token == BCT_ERROR) {
      return NULL;
    }
    client = (BsrClient *)malloc(sizeof(BsrClient));
    memset(client, 0, sizeof(BsrClient));
    bstrncpy(client->ClientName, lc->str, sizeof(client->ClientName));

    /*
     * Add it to the end of the client chain
     */
    if (!bsr->client) {
      bsr->client = client;
    } else {
      BsrClient *bc = bsr->client;
      for (; bc->next; bc = bc->next) {
      }
      bc->next = client;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_job(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrJob *job;

  for (;;) {
    token = LexGetToken(lc, BCT_NAME);
    if (token == BCT_ERROR) {
      return NULL;
    }
    job = (BsrJob *)malloc(sizeof(BsrJob));
    memset(job, 0, sizeof(BsrJob));
    bstrncpy(job->Job, lc->str, sizeof(job->Job));

    /*
     * Add it to the end of the client chain
     */
    if (!bsr->job) {
      bsr->job = job;
    } else {
      /*
       * Add to end of chain
       */
      BsrJob *bc = bsr->job;
      for (; bc->next; bc = bc->next) {
      }
      bc->next = job;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_findex(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrFileIndex *findex;

  for (;;) {
    token = LexGetToken(lc, BCT_PINT32_RANGE);
    if (token == BCT_ERROR) {
      return NULL;
    }
    findex = (BsrFileIndex *)malloc(sizeof(BsrFileIndex));
    memset(findex, 0, sizeof(BsrFileIndex));
    findex->findex = lc->u.pint32_val;
    findex->findex2 = lc->u2.pint32_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->FileIndex) {
      bsr->FileIndex = findex;
    } else {
      /*
       * Add to end of chain
       */
      BsrFileIndex *bs = bsr->FileIndex;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = findex;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_jobid(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrJobid *jobid;

  for (;;) {
    token = LexGetToken(lc, BCT_PINT32_RANGE);
    if (token == BCT_ERROR) {
      return NULL;
    }
    jobid = (BsrJobid *)malloc(sizeof(BsrJobid));
    memset(jobid, 0, sizeof(BsrJobid));
    jobid->JobId = lc->u.pint32_val;
    jobid->JobId2 = lc->u2.pint32_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->JobId) {
      bsr->JobId = jobid;
    } else {
      /*
       * Add to end of chain
       */
      BsrJobid *bs = bsr->JobId;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = jobid;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_count(LEX *lc, BootStrapRecord *bsr) {

  int token;

  token = LexGetToken(lc, BCT_PINT32);
  if (token == BCT_ERROR) {
    return NULL;
  }
  bsr->count = lc->u.pint32_val;
  ScanToEol(lc);
  return bsr;
}

static BootStrapRecord *store_fileregex(LEX *lc, BootStrapRecord *bsr) {

  int token;
  int rc;

  token = LexGetToken(lc, BCT_STRING);
  if (token == BCT_ERROR) {
    return NULL;
  }

  if (bsr->fileregex) free(bsr->fileregex);
  bsr->fileregex = bstrdup(lc->str);

  if (bsr->fileregex_re == NULL) bsr->fileregex_re = (regex_t *)bmalloc(sizeof(regex_t));

  rc = regcomp(bsr->fileregex_re, bsr->fileregex, REG_EXTENDED | REG_NOSUB);
  if (rc != 0) {
    char prbuf[500];
    regerror(rc, bsr->fileregex_re, prbuf, sizeof(prbuf));
    Emsg2(M_ERROR, 0, _("REGEX '%s' compile error. ERR=%s\n"), bsr->fileregex, prbuf);
    return NULL;
  }
  return bsr;
}

static BootStrapRecord *StoreJobtype(LEX *lc, BootStrapRecord *bsr) {



  /* *****FIXME****** */
  Pmsg0(-1, _("JobType not yet implemented\n"));
  return bsr;
}

static BootStrapRecord *store_joblevel(LEX *lc, BootStrapRecord *bsr) {

  /* *****FIXME****** */
  Pmsg0(-1, _("JobLevel not yet implemented\n"));
  return bsr;
}

/*
 * Routine to handle Volume start/end file
 */
static BootStrapRecord *store_volfile(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrVolumeFile *volfile;

  for (;;) {
    token = LexGetToken(lc, BCT_PINT32_RANGE);
    if (token == BCT_ERROR) {
      return NULL;
    }
    volfile = (BsrVolumeFile *)malloc(sizeof(BsrVolumeFile));
    memset(volfile, 0, sizeof(BsrVolumeFile));
    volfile->sfile = lc->u.pint32_val;
    volfile->efile = lc->u2.pint32_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->volfile) {
      bsr->volfile = volfile;
    } else {
      /*
       * Add to end of chain
       */
      BsrVolumeFile *bs = bsr->volfile;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = volfile;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

/*
 * Routine to handle Volume start/end Block
 */
static BootStrapRecord *store_volblock(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrVolumeBlock *volblock;

  for (;;) {
    token = LexGetToken(lc, BCT_PINT32_RANGE);
    if (token == BCT_ERROR) {
      return NULL;
    }
    volblock = (BsrVolumeBlock *)malloc(sizeof(BsrVolumeBlock));
    memset(volblock, 0, sizeof(BsrVolumeBlock));
    volblock->sblock = lc->u.pint32_val;
    volblock->eblock = lc->u2.pint32_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->volblock) {
      bsr->volblock = volblock;
    } else {
      /*
       * Add to end of chain
       */
      BsrVolumeBlock *bs = bsr->volblock;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = volblock;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

/*
 * Routine to handle Volume start/end address
 */
static BootStrapRecord *store_voladdr(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrVolumeAddress *voladdr;

  for (;;) {
    token = LexGetToken(lc, BCT_PINT64_RANGE);
    if (token == BCT_ERROR) {
      return NULL;
    }
    voladdr = (BsrVolumeAddress *)malloc(sizeof(BsrVolumeAddress));
    memset(voladdr, 0, sizeof(BsrVolumeAddress));
    voladdr->saddr = lc->u.pint64_val;
    voladdr->eaddr = lc->u2.pint64_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->voladdr) {
      bsr->voladdr = voladdr;
    } else {
      /*
       * Add to end of chain
       */
      BsrVolumeAddress *bs = bsr->voladdr;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = voladdr;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_sessid(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrSessionId *sid;

  for (;;) {
    token = LexGetToken(lc, BCT_PINT32_RANGE);
    if (token == BCT_ERROR) {
      return NULL;
    }
    sid = (BsrSessionId *)malloc(sizeof(BsrSessionId));
    memset(sid, 0, sizeof(BsrSessionId));
    sid->sessid = lc->u.pint32_val;
    sid->sessid2 = lc->u2.pint32_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->sessid) {
      bsr->sessid = sid;
    } else {
      /*
       * Add to end of chain
       */
      BsrSessionId *bs = bsr->sessid;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = sid;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_sesstime(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrSessionTime *stime;

  for (;;) {
    token = LexGetToken(lc, BCT_PINT32);
    if (token == BCT_ERROR) {
      return NULL;
    }
    stime = (BsrSessionTime *)malloc(sizeof(BsrSessionTime));
    memset(stime, 0, sizeof(BsrSessionTime));
    stime->sesstime = lc->u.pint32_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->sesstime) {
      bsr->sesstime = stime;
    } else {
      /*
       * Add to end of chain
       */
      BsrSessionTime *bs = bsr->sesstime;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = stime;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_stream(LEX *lc, BootStrapRecord *bsr) {

  int token;
  BsrStream *stream;

  for (;;) {
    token = LexGetToken(lc, BCT_INT32);
    if (token == BCT_ERROR) {
      return NULL;
    }
    stream = (BsrStream *)malloc(sizeof(BsrStream));
    memset(stream, 0, sizeof(BsrStream));
    stream->stream = lc->u.int32_val;

    /*
     * Add it to the end of the chain
     */
    if (!bsr->stream) {
      bsr->stream = stream;
    } else {
      /*
       * Add to end of chain
       */
      BsrStream *bs = bsr->stream;
      for (; bs->next; bs = bs->next) {
      }
      bs->next = stream;
    }
    token = LexGetToken(lc, BCT_ALL);
    if (token != BCT_COMMA) {
      break;
    }
  }
  return bsr;
}

static BootStrapRecord *store_slot(LEX *lc, BootStrapRecord *bsr) {

  int token;

  token = LexGetToken(lc, BCT_PINT32);
  if (token == BCT_ERROR) {
    return NULL;
  }
  if (!bsr->volume) {
    Emsg1(M_ERROR, 0, _("Slot %d in bsr at inappropriate place.\n"), lc->u.pint32_val);
    return bsr;
  }
  bsr->volume->Slot = lc->u.pint32_val;
  ScanToEol(lc);
  return bsr;
}

static BootStrapRecord *store_include(LEX *lc, BootStrapRecord *bsr) {

  ScanToEol(lc);
  return bsr;
}

static BootStrapRecord *store_exclude(LEX *lc, BootStrapRecord *bsr) {

  ScanToEol(lc);
  return bsr;
}

static inline void DumpVolfile(BsrVolumeFile *volfile) {




  if (volfile) {
    Pmsg2(-1, _("VolFile     : %u-%u\n"), volfile->sfile, volfile->efile);
    DumpVolfile(volfile->next);
  }
}

static inline void DumpVolblock(BsrVolumeBlock *volblock) {




  if (volblock) {
    Pmsg2(-1, _("VolBlock    : %u-%u\n"), volblock->sblock, volblock->eblock);
    DumpVolblock(volblock->next);
  }
}

static inline void DumpVoladdr(BsrVolumeAddress *voladdr) {




  if (voladdr) {
    Pmsg2(-1, _("VolAddr    : %llu-%llu\n"), voladdr->saddr, voladdr->eaddr);
    DumpVoladdr(voladdr->next);
  }
}

static inline void DumpFindex(BsrFileIndex *FileIndex) {




  if (FileIndex) {
    if (FileIndex->findex == FileIndex->findex2) {
      Pmsg1(-1, _("FileIndex   : %u\n"), FileIndex->findex);
    } else {
      Pmsg2(-1, _("FileIndex   : %u-%u\n"), FileIndex->findex, FileIndex->findex2);
    }
    DumpFindex(FileIndex->next);
  }
}

static inline void DumpJobid(BsrJobid *jobid) {




  if (jobid) {
    if (jobid->JobId == jobid->JobId2) {
      Pmsg1(-1, _("JobId       : %u\n"), jobid->JobId);
    } else {
      Pmsg2(-1, _("JobId       : %u-%u\n"), jobid->JobId, jobid->JobId2);
    }
    DumpJobid(jobid->next);
  }
}

static inline void DumpSessid(BsrSessionId *sessid) {




  if (sessid) {
    if (sessid->sessid == sessid->sessid2) {
      Pmsg1(-1, _("SessId      : %u\n"), sessid->sessid);
    } else {
      Pmsg2(-1, _("SessId      : %u-%u\n"), sessid->sessid, sessid->sessid2);
    }
    DumpSessid(sessid->next);
  }
}

static inline void DumpVolume(BsrVolume *volume) {




  if (volume) {
    Pmsg1(-1, _("VolumeName  : %s\n"), volume->VolumeName);
    Pmsg1(-1, _("  MediaType : %s\n"), volume->MediaType);
    Pmsg1(-1, _("  Device    : %s\n"), volume->device);
    Pmsg1(-1, _("  Slot      : %d\n"), volume->Slot);
    DumpVolume(volume->next);
  }
}

static inline void DumpClient(BsrClient *client) {




  if (client) {
    Pmsg1(-1, _("Client      : %s\n"), client->ClientName);
    DumpClient(client->next);
  }
}

static inline void dump_job(BsrJob *job) {

  if (job) {
    Pmsg1(-1, _("Job          : %s\n"), job->Job);
    dump_job(job->next);
  }
}

static inline void DumpSesstime(BsrSessionTime *sesstime) {




  if (sesstime) {
    Pmsg1(-1, _("SessTime    : %u\n"), sesstime->sesstime);
    DumpSesstime(sesstime->next);
  }
}

void DumpBsr(BootStrapRecord *bsr, bool recurse) {




  int save_debug = debug_level;
  debug_level = 1;
  if (!bsr) {
    Pmsg0(-1, _("BootStrapRecord is NULL\n"));
    debug_level = save_debug;
    return;
  }
  Pmsg1(-1, _("Next        : 0x%x\n"), bsr->next);
  Pmsg1(-1, _("Root bsr    : 0x%x\n"), bsr->root);
  DumpVolume(bsr->volume);
  DumpSessid(bsr->sessid);
  DumpSesstime(bsr->sesstime);
  DumpVolfile(bsr->volfile);
  DumpVolblock(bsr->volblock);
  DumpVoladdr(bsr->voladdr);
  DumpClient(bsr->client);
  DumpJobid(bsr->JobId);
  dump_job(bsr->job);
  DumpFindex(bsr->FileIndex);
  if (bsr->count) {
    Pmsg1(-1, _("count       : %u\n"), bsr->count);
    Pmsg1(-1, _("found       : %u\n"), bsr->found);
  }

  Pmsg1(-1, _("done        : %s\n"), bsr->done ? _("yes") : _("no"));
  Pmsg1(-1, _("positioning : %d\n"), bsr->use_positioning);
  Pmsg1(-1, _("fast_reject : %d\n"), bsr->use_fast_rejection);
  if (recurse && bsr->next) {
    Pmsg0(-1, "\n");
    DumpBsr(bsr->next, true);
  }
  debug_level = save_debug;
}

/*
 * Free bsr resources
 */
static inline void FreeBsrItem(BootStrapRecord *bsr) {




  if (bsr) {
    FreeBsrItem(bsr->next);
    free(bsr);
  }
}

/*
 * Remove a single item from the bsr tree
 */
static inline void RemoveBsr(BootStrapRecord *bsr) {




  FreeBsrItem((BootStrapRecord *)bsr->volume);
  FreeBsrItem((BootStrapRecord *)bsr->client);
  FreeBsrItem((BootStrapRecord *)bsr->sessid);
  FreeBsrItem((BootStrapRecord *)bsr->sesstime);
  FreeBsrItem((BootStrapRecord *)bsr->volfile);
  FreeBsrItem((BootStrapRecord *)bsr->volblock);
  FreeBsrItem((BootStrapRecord *)bsr->voladdr);
  FreeBsrItem((BootStrapRecord *)bsr->JobId);
  FreeBsrItem((BootStrapRecord *)bsr->job);
  FreeBsrItem((BootStrapRecord *)bsr->FileIndex);
  FreeBsrItem((BootStrapRecord *)bsr->JobType);
  FreeBsrItem((BootStrapRecord *)bsr->JobLevel);
  if (bsr->fileregex) {
    bfree(bsr->fileregex);
  }
  if (bsr->fileregex_re) {
    regfree(bsr->fileregex_re);
    free(bsr->fileregex_re);
  }
  if (bsr->attr) {
    FreeAttr(bsr->attr);
  }
  if (bsr->next) {
    bsr->next->prev = bsr->prev;
  }
  if (bsr->prev) {
    bsr->prev->next = bsr->next;
  }
  free(bsr);
}

/*
 * Free all bsrs in chain
 */
void FreeBsr(BootStrapRecord *bsr) {




  BootStrapRecord *next_bsr;

  if (!bsr) {
    return;
  }
  next_bsr = bsr->next;

  /*
   * Remove (free) current bsr
   */
  RemoveBsr(bsr);

  /*
   * Now get the next one
   */
  FreeBsr(next_bsr);
}
