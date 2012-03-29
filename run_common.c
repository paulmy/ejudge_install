/* -*- c -*- */
/* $Id$ */

/* Copyright (C) 2012 Alexander Chernov <cher@ejudge.ru> */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "config.h"
#include "ej_limits.h"

#include "misctext.h"
#include "prepare.h"
#include "run_packet.h"
#include "super_run_packet.h"
#include "run.h"
#include "errlog.h"
#include "runlog.h"
#include "digest_io.h"
#include "fileutl.h"
#include "testinfo.h"
#include "full_archive.h"
#include "win32_compat.h"
#include "ejudge_cfg.h"
#include "cr_serialize.h"
#include "interrupt.h"
#include "nwrun_packet.h"
#include "filehash.h"
#include "curtime.h"

#include "reuse_xalloc.h"
#include "reuse_osdeps.h"
#include "reuse_exec.h"
#include "reuse_logger.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef __MINGW32__
#include <sys/vfs.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#define SIZE_G (1024 * 1024 * 1024)
#define SIZE_M (1024 * 1024)
#define SIZE_K (1024)

static unsigned char*
size_t_to_size(unsigned char *buf, size_t buf_size, size_t num)
{
  if (!num) snprintf(buf, buf_size, "0");
  else if (!(num % SIZE_G)) snprintf(buf, buf_size, "%" EJ_PRINTF_ZSPEC "uG", EJ_PRINTF_ZCAST(num / SIZE_G));
  else if (!(num % SIZE_M)) snprintf(buf, buf_size, "%" EJ_PRINTF_ZSPEC "uM", EJ_PRINTF_ZCAST(num / SIZE_M));
  else if (!(num % SIZE_K)) snprintf(buf, buf_size, "%" EJ_PRINTF_ZSPEC "uK", EJ_PRINTF_ZCAST(num / SIZE_K));
  else snprintf(buf, buf_size, "%" EJ_PRINTF_ZSPEC "u", EJ_PRINTF_ZCAST(num));
  return buf;
}

static unsigned char *
prepare_checker_comment(int utf8_mode, const unsigned char *str)
{
  size_t len = strlen(str);
  unsigned char *wstr = 0, *p;
  unsigned char *cmt = 0;

  wstr = (unsigned char*) xmalloc(len + 1);
  strcpy(wstr, str);
  for (p = wstr; *p; p++)
    if (*p < ' ') *p = ' ';
  for (--p; p >= wstr && *p == ' '; *p-- = 0);
  for (p = wstr; *p; p++) {
    switch (*p) {
    case '"': case '&': case '<': case '>':
      *p = '?';
    }
  }
  if (utf8_mode) {
    utf8_fix_string(wstr, 0);
    len = strlen(wstr);
    if (len > 128) {
      p = wstr + 120;
      while (*p >= 0x80 && *p <= 0xbf) p--; 
      *p++ = '.';
      *p++ = '.';
      *p++ = '.';
      *p = 0;
    }
  } else {
    if (p - wstr > 64) {
      p = wstr + 60;
      *p++ = '.';
      *p++ = '.';
      *p++ = '.';
      *p = 0;
    }
  }

  cmt = html_armor_string_dup(wstr);
  xfree(wstr);
  return cmt;
}

static const char * const scoring_system_strs[] =
{
  [SCORE_ACM] "ACM",
  [SCORE_KIROV] "KIROV",
  [SCORE_OLYMPIAD] "OLYMPIAD",
  [SCORE_MOSCOW] "MOSCOW",
};
static const unsigned char *
unparse_scoring_system(unsigned char *buf, size_t size, int val)
{
  if (val >= SCORE_ACM && val < SCORE_TOTAL) return scoring_system_strs[val];
  snprintf(buf, size, "scoring_%d", val);
  return buf;
}

#define ARMOR(s)  html_armor_buf(&ab, s)

static int
generate_xml_report(
        const struct super_run_in_packet *srp,
        struct run_reply_packet *reply_pkt,
        const unsigned char *report_path,
        int total_tests,
        const struct testinfo *tests,
        int utf8_mode,
        int variant,
        int scores,
        int max_score,
        int user_max_score,
        int correct_available_flag,
        int info_available_flag,
        int report_time_limit_ms,
        int report_real_time_limit_ms,
        int has_real_time,
        int has_max_memory_used,
        int marked_flag,
        int user_run_tests,
        const unsigned char *additional_comment,
        const unsigned char *valuer_comment,
        const unsigned char *valuer_judge_comment,
        const unsigned char *valuer_errors)
{
  FILE *f = 0;
  unsigned char buf1[32], buf2[32], buf3[128];
  int i;
  unsigned char *msg = 0;
  struct html_armor_buffer ab = HTML_ARMOR_INITIALIZER;
  const struct super_run_in_global_packet *srgp = srp->global;

  if (!(f = fopen(report_path, "w"))) {
    err("generate_xml_report: cannot open protocol file %s", report_path);
    return -1;
  }

  fprintf(f, "Content-type: text/xml\n\n");
  fprintf(f, "<?xml version=\"1.0\" encoding=\"%s\"?>\n", EJUDGE_CHARSET);

  run_status_to_str_short(buf1, sizeof(buf1), reply_pkt->status);
  fprintf(f, "<testing-report run-id=\"%d\" judge-id=\"%d\" status=\"%s\" scoring=\"%s\" archive-available=\"%s\" run-tests=\"%d\"",
          srgp->run_id, srgp->judge_id, buf1,
          unparse_scoring_system(buf2, sizeof(buf2), srgp->scoring_system_val),
          (srgp->enable_full_archive)?"yes":"no", total_tests - 1);
  if (has_real_time) {
    fprintf(f, " real-time-available=\"yes\"");
  }
  if (has_max_memory_used) {
    fprintf(f, " max-memory-used-available=\"yes\"");
  }
  if (correct_available_flag) {
    fprintf(f, " correct-available=\"yes\"");
  }
  if (info_available_flag) {
    fprintf(f, " info-available=\"yes\"");
  }
  if (variant > 0) {
    fprintf(f, " variant=\"%d\"", variant);
  }
  if (srgp->scoring_system_val == SCORE_OLYMPIAD) {
    fprintf(f, " accepting-mode=\"%s\"", srgp->accepting_mode?"yes":"no");
  }
  if (srgp->scoring_system_val == SCORE_OLYMPIAD && srgp->accepting_mode
      && reply_pkt->status != RUN_ACCEPTED) {
    fprintf(f, " failed-test=\"%d\"", total_tests - 1);
  } else if (srgp->scoring_system_val == SCORE_ACM && reply_pkt->status != RUN_OK) {
    fprintf(f, " failed-test=\"%d\"", total_tests - 1);
  } else if (srgp->scoring_system_val == SCORE_OLYMPIAD && !srgp->accepting_mode) {
    fprintf(f, " tests-passed=\"%d\" score=\"%d\" max-score=\"%d\"",
            reply_pkt->failed_test - 1, reply_pkt->score, max_score);
  } else if (srgp->scoring_system_val == SCORE_KIROV) {
    fprintf(f, " tests-passed=\"%d\" score=\"%d\" max-score=\"%d\"",
            reply_pkt->failed_test - 1, reply_pkt->score, max_score);
  } else if (srgp->scoring_system_val == SCORE_MOSCOW) {
    if (reply_pkt->status != RUN_OK) {
      fprintf(f, " failed-test=\"%d\"", total_tests - 1);
    }
    fprintf(f, " score=\"%d\" max-score=\"%d\"", reply_pkt->score, max_score);
  }
  if (report_time_limit_ms > 0) {
    fprintf(f, " time-limit-ms=\"%d\"", report_time_limit_ms);
  }
  if (report_real_time_limit_ms > 0) {
    fprintf(f, " real-time-limit-ms=\"%d\"", report_real_time_limit_ms);
  }
  if (marked_flag >= 0) {
    fprintf(f, " marked-flag=\"%s\"", marked_flag?"yes":"no");
  }
  if (srgp->separate_user_score > 0 && reply_pkt->user_status >= 0) {
    run_status_to_str_short(buf1, sizeof(buf1), reply_pkt->user_status);
    fprintf(f, " user-status=\"%s\"", buf1);
  }
  if (srgp->separate_user_score > 0 && reply_pkt->user_score >= 0) {
    fprintf(f, " user-score=\"%d\"", reply_pkt->user_score);
  }
  if (srgp->separate_user_score > 0) {
    if (user_max_score < 0) user_max_score = max_score;
    fprintf(f, " user-max-score=\"%d\"", user_max_score);
  }
  if (srgp->separate_user_score > 0 && reply_pkt->user_tests_passed >= 0) {
    fprintf(f, " user-tests-passed=\"%d\"", reply_pkt->user_tests_passed);
  }
  if (srgp->separate_user_score > 0 && user_run_tests >= 0) {
    fprintf(f, " user-run-tests=\"%d\"", user_run_tests);
  }
  fprintf(f, " >\n");

  if (additional_comment) {
    fprintf(f, "  <comment>%s</comment>\n", ARMOR(additional_comment));
  }
  if (valuer_comment) {
    fprintf(f, "  <valuer-comment>%s</valuer-comment>\n",
            ARMOR(valuer_comment));
  }
  if (valuer_judge_comment) {
    fprintf(f, "  <valuer-judge-comment>%s</valuer-judge-comment>\n",
            ARMOR(valuer_judge_comment));
  }
  if (valuer_errors) {
    fprintf(f, "  <valuer-errors>%s</valuer-errors>\n",
            ARMOR(valuer_errors));
  }
  if ((msg = os_NodeName())) {
    fprintf(f, "  <host>%s</host>\n", msg);
  }

  fprintf(f, "  <tests>\n");

  for (i = 1; i < total_tests; i++) {
    run_status_to_str_short(buf1, sizeof(buf1), tests[i].status);
    fprintf(f, "    <test num=\"%d\" status=\"%s\"", i, buf1);
    if (tests[i].status == RUN_RUN_TIME_ERR) {
      if (tests[i].code == 256) {
        fprintf(f, " term-signal=\"%d\"", tests[i].termsig);
      } else {
        fprintf(f, " exit-code=\"%d\"", tests[i].code);
      }
    }
    fprintf(f, " time=\"%lu\"", tests[i].times);
    if (tests[i].real_time >= 0 && has_real_time) {
      fprintf(f, " real-time=\"%ld\"", tests[i].real_time);
    }
    if (tests[i].max_memory_used > 0) {
      fprintf(f, " max-memory-used=\"%d\"", tests[i].max_memory_used);
    }
    if (srgp->scoring_system_val == SCORE_OLYMPIAD && !srgp->accepting_mode) {
      fprintf(f, " nominal-score=\"%d\" score=\"%d\"",
              tests[i].max_score, tests[i].score);
    } else if (srgp->scoring_system_val == SCORE_KIROV) {
      fprintf(f, " nominal-score=\"%d\" score=\"%d\"",
              tests[i].max_score, tests[i].score);
    }
    if (tests[i].comment && tests[i].comment[0]) {
      fprintf(f, " comment=\"%s\"", ARMOR(tests[i].comment));
    }
    if (tests[i].team_comment && tests[i].team_comment[0]) {
      fprintf(f, " team-comment=\"%s\"", ARMOR(tests[i].team_comment));
    }
    if (tests[i].exit_comment && tests[i].exit_comment[0]) {
      fprintf(f, " exit-comment=\"%s\"", ARMOR(tests[i].exit_comment));
    }
    if ((tests[i].status == RUN_WRONG_ANSWER_ERR 
         || tests[i].status == RUN_PRESENTATION_ERR || tests[i].status == RUN_OK)
        && tests[i].chk_out_size > 0 && tests[i].chk_out && tests[i].chk_out[0]) {
      msg = prepare_checker_comment(utf8_mode, tests[i].chk_out);
      fprintf(f, " checker-comment=\"%s\"", msg);
      xfree(msg);
    }
    if (srgp->enable_full_archive) {
      if (tests[i].has_input_digest) {
        digest_to_ascii(DIGEST_SHA1, tests[i].input_digest, buf3);
        fprintf(f, " input-digest=\"%s\"", buf3);
      }
      if (tests[i].has_correct_digest) {
        digest_to_ascii(DIGEST_SHA1, tests[i].correct_digest, buf3);
        fprintf(f, " correct-digest=\"%s\"", buf3);
      }
      if (tests[i].has_info_digest) {
        digest_to_ascii(DIGEST_SHA1, tests[i].info_digest, buf3);
        fprintf(f, " info-digest=\"%s\"", buf3);
      }
    }
    if (tests[i].output_size >= 0 && srgp->enable_full_archive) {
      fprintf(f, " output-available=\"yes\"");
    }
    if (tests[i].error_size >= 0 && srgp->enable_full_archive) {
      fprintf(f, " stderr-available=\"yes\"");
    }
    if (tests[i].chk_out_size >= 0 && srgp->enable_full_archive) {
      fprintf(f, " checker-output-available=\"yes\"");
    }
    if (tests[i].args && strlen(tests[i].args) >= srgp->max_cmd_length) {
      fprintf(f, " args-too-long=\"yes\"");
    }
    if (tests[i].visibility > 0) {
      fprintf(f, " visibility=\"%s\"", test_visibility_unparse(tests[i].visibility));
    }
    fprintf(f, " >\n");

    if (tests[i].args && strlen(tests[i].args) < srgp->max_cmd_length) {
      fprintf(f, "      <args>%s</args>\n", ARMOR(tests[i].args));
    }

    if (tests[i].input_size >= 0 && !srgp->enable_full_archive) {
      fprintf(f, "      <input>");
      html_print_by_line(f, utf8_mode, srgp->max_file_length,
                         srgp->max_line_length,
                         tests[i].input, tests[i].input_size);
      fprintf(f, "</input>\n");
    }

    if (tests[i].output_size >= 0 && !srgp->enable_full_archive) {
      fprintf(f, "      <output>");
      html_print_by_line(f, utf8_mode, srgp->max_file_length,
                         srgp->max_line_length,
                         tests[i].output, tests[i].output_size);
      fprintf(f, "</output>\n");
    }

    if (tests[i].correct_size >= 0 && !srgp->enable_full_archive) {
      fprintf(f, "      <correct>");
      html_print_by_line(f, utf8_mode, srgp->max_file_length,
                         srgp->max_line_length,
                         tests[i].correct, tests[i].correct_size);
      fprintf(f, "</correct>\n");
    }

    if (tests[i].error_size >= 0 && !srgp->enable_full_archive) {
      fprintf(f, "      <stderr>");
      html_print_by_line(f, utf8_mode, srgp->max_file_length,
                         srgp->max_line_length,
                         tests[i].error, tests[i].error_size);
      fprintf(f, "</stderr>\n");
    }

    if (tests[i].chk_out_size >= 0 && !srgp->enable_full_archive) {
      fprintf(f, "      <checker>");
      html_print_by_line(f, utf8_mode, srgp->max_file_length,
                         srgp->max_line_length,
                         tests[i].chk_out, tests[i].chk_out_size);
      fprintf(f, "</checker>\n");
    }

    fprintf(f, "    </test>\n");
  }

  fprintf(f, "  </tests>\n");

  fprintf(f, "</testing-report>\n");
  fclose(f); f = 0;
  html_armor_free(&ab);
  return 0;
}

static int
read_error_code(char const *path)
{
  FILE *f;
  int   n;

  if (!(f = fopen(path, "r"))) {
    return 100;
  }
  if (fscanf(f, "%d", &n) != 1) {
    fclose(f);
    return 101;
  }
  fscanf(f, " ");
  if (getc(f) != EOF) {
    fclose(f);
    return 102;
  }
  fclose(f);
  return n;
}

static void
append_msg_to_log(const unsigned char *path, const char *format, ...)
{
  va_list args;
  unsigned char buf[1024];
  FILE *f;

  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (!(f = fopen(path, "a"))) {
    err("append_msg_to_log: cannot open %s for appending", path);
    return;
  }
  fprintf(f, "\n\nrun: %s\n", buf);
  if (ferror(f)) {
    err("append_msg_to_log: write error to %s", path);
    fclose(f);
    return;
  }
  if (fclose(f) < 0) {
    err("append_msg_to_log: write error to %s", path);
    return;
  }
}

static void
chk_printf(struct testinfo *result, const char *format, ...)
{
  va_list args;
  unsigned char buf[1024];

  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (!result->chk_out) {
    result->chk_out = xstrdup(buf);
    result->chk_out_size = strlen(result->chk_out);
  } else {
    int len1 = strlen(result->chk_out);
    int len2 = strlen(buf);
    int len3 = len1 + len2;
    unsigned char *str = (unsigned char*) xmalloc(len3 + 1);
    memcpy(str, result->chk_out, len1);
    memcpy(str + len1, buf, len2);
    str[len3] = 0;
    xfree(result->chk_out);
    result->chk_out = str;
    result->chk_out_size = len3;
  }
}

static int
read_checker_score(
        const unsigned char *path,
        const unsigned char *log_path,
        const unsigned char *what,
        int max_score,
        int *p_score)
{
  char *score_buf = 0;
  size_t score_buf_size = 0;
  int x, n, r;

  r = generic_read_file(&score_buf, 0, &score_buf_size, 0,
                        0, path, "");
  if (r < 0) {
    append_msg_to_log(log_path, "Cannot read the %s score output", what);
    return -1;
  }
  if (strlen(score_buf) != score_buf_size) {
    append_msg_to_log(log_path, "The %s score output is binary", what);
    xfree(score_buf);
    return -1;
  }

  while (score_buf_size > 0 && isspace(score_buf[score_buf_size - 1]))
    score_buf[--score_buf_size] = 0;
  if (!score_buf_size) {
    append_msg_to_log(log_path, "The %s score output is empty", what);
    xfree(score_buf);
    return -1;
  }

  if (sscanf(score_buf, "%d%n", &x, &n) != 1 || score_buf[n]) {
    append_msg_to_log(log_path, "The %s score output (%s) is invalid",
                      what, score_buf);
    xfree(score_buf);
    return -1;
  }
  if (x < 0 || x > max_score) {
    append_msg_to_log(log_path, "The %s score (%d) is invalid", what, x);
    xfree(score_buf);
    return -1;
  }

  *p_score = x;
  xfree(score_buf);
  return 0;
}

static int
read_valuer_score(
        const unsigned char *path,
        const unsigned char *log_path,
        const unsigned char *what,
        int max_score,
        int valuer_sets_marked,
        int separate_user_score,
        int *p_score,
        int *p_marked,
        int *p_user_status,
        int *p_user_score,
        int *p_user_tests_passed)
{
  char *score_buf = 0, *p;
  size_t score_buf_size = 0;
  int x, y, n, r, user_status = -1, user_score = -1, user_tests_passed = -1;

  if (p_marked) *p_marked = -1;

  r = generic_read_file(&score_buf, 0, &score_buf_size, 0,
                        0, path, "");
  if (r < 0) {
    append_msg_to_log(log_path, "Cannot read the %s score output", what);
    return -1;
  }
  if (strlen(score_buf) != score_buf_size) {
    append_msg_to_log(log_path, "The %s score output is binary", what);
    goto fail;
  }

  while (score_buf_size > 0 && isspace(score_buf[score_buf_size - 1]))
    score_buf[--score_buf_size] = 0;
  if (!score_buf_size) {
    append_msg_to_log(log_path, "The %s score output is empty", what);
    goto fail;
  }

  p = score_buf;
  if (sscanf(p, "%d%n", &x, &n) != 1) {
    append_msg_to_log(log_path, "The %s score output (%s) is invalid",
                      what, score_buf);
    goto fail;
  }
  if (x < 0 || x > max_score) {
    append_msg_to_log(log_path, "The %s score (%d) is invalid", what, x);
    goto fail;
  }
  p += n;

  if (valuer_sets_marked > 0) {
    if (sscanf(p, "%d%n", &y, &n) != 1) {
      append_msg_to_log(log_path, "The %s marked_flag output (%s) is invalid",
                        what, score_buf);
      goto fail;
    }
    if (y < 0 || y > 1) {
      append_msg_to_log(log_path, "The %s marked_flag (%d) is invalid", what,y);
      goto fail;
    }
    p += n;
  }

  if (separate_user_score > 0) {
    while (isspace(*p)) ++p;
    if (*p) {
      if (sscanf(p, "%d%n", &user_status, &n) != 1) {
        append_msg_to_log(log_path, "The %s user_status output (%s) is invalid",
                          what, score_buf);
        goto fail;
      }
      p += n;
      if (user_status >= 0) {
        if (user_status != RUN_OK && user_status != RUN_PARTIAL) {
          append_msg_to_log(log_path, "The %s user_status output (%d) is invalid",
                            what, user_status);
          goto fail;
        }
      } else {
        user_status = -1;
      }
    }
    while (isspace(*p)) ++p;
    if (*p) {
      if (sscanf(p, "%d%n", &user_score, &n) != 1) {
        append_msg_to_log(log_path, "The %s user_score output (%s) is invalid",
                          what, score_buf);
        goto fail;
      }
      p += n;
      if (user_score >= 0) {
        // do some more checking...
      } else {
        user_score = -1;
      }
    }
    while (isspace(*p)) ++p;
    if (*p) {
      if (sscanf(p, "%d%n", &user_tests_passed, &n) != 1) {
        append_msg_to_log(log_path, "The %s user_tests_passed output (%s) is invalid",
                          what, score_buf);
        goto fail;
      }
      p += n;
      if (user_tests_passed >= 0) {
        // do some more checking
      } else {
        user_tests_passed = -1;
      }
    }
  }

  if (*p) {
    append_msg_to_log(log_path, "The %s output is invalid", what);
    goto fail;
  }

  *p_score = x;
  if (valuer_sets_marked > 0 && p_marked) *p_marked = y;
  if (separate_user_score > 0) {
    if (p_user_status && user_status >= 0) *p_user_status = user_status;
    if (p_user_score && user_score >= 0) *p_user_score = user_score;
    if (p_user_tests_passed && user_tests_passed >= 0) *p_user_tests_passed = user_tests_passed;
  }

  xfree(score_buf);
  return 0;

fail:
  xfree(score_buf);
  return -1;
}

static void
setup_environment(
        tpTask tsk,
        char **envs,
        const unsigned char *ejudge_prefix_dir_env,
        const struct testinfo_struct *pt)
{
  int jj;
  unsigned char env_buf[1024];
  const unsigned char *envval = NULL;
  
  if (envs) {
    for (jj = 0; envs[jj]; jj++) {
      if (!strcmp(envs[jj], "EJUDGE_PREFIX_DIR")) {
        task_PutEnv(tsk, ejudge_prefix_dir_env);
      } else if (!strchr(envs[jj], '=')) {
        envval = getenv(envs[jj]);
        if (envval) {
          snprintf(env_buf, sizeof(env_buf), "%s=%s", envs[jj], envval);
          task_PutEnv(tsk, env_buf);
        }
      } else {
        task_PutEnv(tsk, envs[jj]);
      }
    }
  }

  if (pt && pt->env_u && pt->env_v) {
    for (jj = 0; jj < pt->env_u; ++jj) {
      if (pt->env_v[jj]) {
        task_PutEnv(tsk, pt->env_v[jj]);
      }
    }
  }
}

static int
invoke_valuer(
        const struct section_global_data *global,
        const struct super_run_in_packet *srp,
        int total_tests,
        const struct testinfo *tests,
        int cur_variant,
        int max_score,
        int *p_score,
        int *p_marked,
        int *p_user_status,
        int *p_user_score,
        int *p_user_tests_passed,
        char **p_err_txt,
        char **p_cmt_txt,
        char **p_jcmt_txt)
{
  path_t ejudge_prefix_dir_env;
  path_t score_list;
  path_t score_res;
  path_t score_err;
  path_t score_cmt;
  path_t score_jcmt;
  path_t valuer_cmd;
  FILE *f = 0;
  int i, retval = -1;
  tpTask tsk = 0;
  char *err_txt = 0, *cmt_txt = 0, *jcmt_txt = 0;
  size_t err_len = 0, cmt_len = 0, jcmt_len = 0;
  unsigned char strbuf[1024];

  const struct super_run_in_global_packet *srgp = srp->global;
  const struct super_run_in_problem_packet *srpp = srp->problem;

  ejudge_prefix_dir_env[0] = 0;
#ifdef EJUDGE_PREFIX_DIR
  snprintf(ejudge_prefix_dir_env, sizeof(ejudge_prefix_dir_env),
           "EJUDGE_PREFIX_DIR=%s", EJUDGE_PREFIX_DIR);
#endif /* EJUDGE_PREFIX_DIR */

  pathmake(score_list, global->run_work_dir, "/", "score_list", NULL);
  pathmake(score_res, global->run_work_dir, "/", "score_res", NULL);
  pathmake(score_err, global->run_work_dir, "/", "score_err", NULL);
  pathmake(score_cmt, global->run_work_dir, "/", "score_cmt", NULL);
  pathmake(score_jcmt, global->run_work_dir, "/", "score_jcmt", NULL);

  // write down the score list
  if (!(f = fopen(score_list, "w"))) {
    append_msg_to_log(score_err, "cannot open %s for writing", score_list);
    goto cleanup;
  }
  fprintf(f, "%d\n", total_tests - 1);
  for (i = 1; i < total_tests; i++) {
    fprintf(f, "%d", tests[i].status);
    if (srpp->scoring_checker > 0) {
      fprintf(f, " %d", tests[i].checker_score);
    } else {
      fprintf(f, " %d", tests[i].score);
    }
    fprintf(f, " %ld", tests[i].times);
    fprintf(f, "\n");
  }
  if (ferror(f)) {
    append_msg_to_log(score_err, "failed to write to %s", score_list);
    goto cleanup;
  }
  if (fclose(f) < 0) {
    append_msg_to_log(score_err, "failed to write to %s", score_list);
    f = 0;
    goto cleanup;
  }
  f = 0;

  snprintf(valuer_cmd, sizeof(valuer_cmd), srpp->valuer_cmd);

  info("starting valuer: %s %s %s", valuer_cmd, score_cmt, score_jcmt);

  tsk = task_New();
  task_AddArg(tsk, valuer_cmd);
  task_AddArg(tsk, score_cmt);
  task_AddArg(tsk, score_jcmt);
  task_SetRedir(tsk, 0, TSR_FILE, score_list, TSK_READ);
  task_SetRedir(tsk, 1, TSR_FILE, score_res, TSK_REWRITE, TSK_FULL_RW);
  task_SetRedir(tsk, 2, TSR_FILE, score_err, TSK_REWRITE, TSK_FULL_RW);
  task_SetWorkingDir(tsk, global->run_work_dir);
  task_SetPathAsArg0(tsk);
  if (srpp->checker_real_time_limit_ms > 0) {
    task_SetMaxRealTimeMillis(tsk, srpp->checker_real_time_limit_ms);
  }
  setup_environment(tsk, srpp->valuer_env, ejudge_prefix_dir_env, NULL);
  if (srgp->separate_user_score > 0) {
    snprintf(strbuf, sizeof(strbuf), "EJUDGE_USER_SCORE=1");
    task_PutEnv(tsk, strbuf);
  }
  task_EnableAllSignals(tsk);

  task_PrintArgs(tsk);

  if (task_Start(tsk) < 0) {
    append_msg_to_log(score_err, "valuer failed to start");
    goto cleanup;
  }
  task_Wait(tsk);
  if (task_IsTimeout(tsk)) {
    append_msg_to_log(score_err, "valuer time-out");
    goto cleanup;
  } else if (task_IsAbnormal(tsk)) {
    if (task_Status(tsk) == TSK_SIGNALED) {
      i = task_TermSignal(tsk);
      append_msg_to_log(score_err, "valuer exited by signal %d (%s)",
                        i, os_GetSignalString(i));
    } else {
      append_msg_to_log(score_err, "valuer exited with code %d",
                        task_ExitCode(tsk));
    }
    goto cleanup;
  }

  task_Delete(tsk); tsk = 0;

  if (read_valuer_score(score_res, score_err, "valuer", max_score,
                        srpp->valuer_sets_marked, srgp->separate_user_score,
                        p_score, p_marked, p_user_status, p_user_score, p_user_tests_passed) < 0) {
    goto cleanup;
  }
  generic_read_file(&cmt_txt, 0, &cmt_len, 0, 0, score_cmt, "");
  if (cmt_txt) {
    while (cmt_len > 0 && isspace(cmt_txt[cmt_len - 1])) cmt_len--;
    cmt_txt[cmt_len] = 0;
    if (!cmt_len) {
      xfree(cmt_txt);
      cmt_txt = 0;
    }
  }
  generic_read_file(&jcmt_txt, 0, &jcmt_len, 0, 0, score_jcmt, "");
  if (jcmt_txt) {
    while (jcmt_len > 0 && isspace(jcmt_txt[jcmt_len - 1])) jcmt_len--;
    jcmt_txt[jcmt_len] = 0;
    if (!jcmt_len) {
      xfree(jcmt_txt);
      jcmt_txt = 0;
    }
  }

  if (p_cmt_txt) {
    *p_cmt_txt = cmt_txt;
    cmt_txt = 0;
  }
  if (p_jcmt_txt) {
    *p_jcmt_txt = jcmt_txt;
    jcmt_txt = 0;
  }
  retval = 0;

 cleanup:
  generic_read_file(&err_txt, 0, &err_len, 0, 0, score_err, "");
  if (err_txt) {
    while (err_len > 0 && isspace(err_txt[err_len - 1])) err_len--;
    err_txt[err_len] = 0;
    if (!err_len) {
      xfree(err_txt); err_txt = 0;
    }
  }

  if (tsk) {
    task_Delete(tsk);
    tsk = 0;
  }

  xfree(cmt_txt); cmt_txt = 0;
  xfree(jcmt_txt); jcmt_txt = 0;
  if (p_err_txt) {
    *p_err_txt = err_txt; err_txt = 0;
  } else {
    xfree(err_txt); err_txt = 0;
  }

  unlink(score_list);
  unlink(score_res);
  unlink(score_err);
  unlink(score_cmt);
  unlink(score_jcmt);
  return retval;
}

static long
get_expected_free_space(const unsigned char *path)
{
#ifdef __MINGW32__
  return -1;
#else
  struct statfs sb;
  if (statfs(path, &sb) < 0) return -1;
  return sb.f_bfree;
#endif
}

static void
check_free_space(const unsigned char *path, long expected_space)
{
#ifndef __MINGW32__
  struct statfs sb;
  int wait_count = 0;

  if (expected_space <= 0) return;

  while (1) {
    if (statfs(path, &sb) < 0) {
      err("statfs failed: %s", os_ErrorMsg());
      return;
    }
    if (sb.f_bfree * 2 >= expected_space) return;
    if (++wait_count == 10) {
      err("check_free_space: waiting for free space aborted after ten attempts!");
      return;
    }
    info("not enough free space in the working directory, waiting");
    os_Sleep(500);
  }
#endif
}

static int
get_num_prefix(int num)
{
  if (num < 0) return '-';
  if (num < 10) return '0';
  if (num < 100) return '1';
  if (num < 1000) return '2';
  if (num < 10000) return '3';
  if (num < 100000) return '4';
  if (num < 1000000) return '5';
  return '6';
}

static const unsigned char b32_digits[]=
"0123456789ABCDEFGHIJKLMNOPQRSTUV";

static int
invoke_nwrun(
        const struct ejudge_cfg *config,
        serve_state_t state,
        const struct section_tester_data *tst,
        const struct super_run_in_packet *srp,
        full_archive_t far,
        int test_num,
        int priority,
        int *p_has_real_time,
        const unsigned char *exe_src_dir,
        const unsigned char *exe_basename,
        const unsigned char *test_src_path,
        const unsigned char *test_basename,
        long time_limit_millis,
        struct testinfo *result)
{
  path_t full_spool_dir;
  path_t pkt_name;
  path_t full_in_path;
  path_t full_dir_path;
  path_t queue_path;
  path_t tmp_in_path;
  path_t exe_src_path;
  path_t result_path;
  path_t result_pkt_name;
  path_t out_entry_packet = { 0 };
  path_t dir_entry_packet;
  path_t check_output_path;
  path_t packet_output_path;
  path_t packet_error_path;
  path_t arch_entry_name;
  FILE *f = 0;
  int r;
  struct generic_section_config *generic_out_packet = 0;
  struct nwrun_out_packet *out_packet = 0;
  long file_size;
  int timeout;
  int wait_time;

  const struct super_run_in_global_packet *srgp = srp->global;
  const struct super_run_in_problem_packet *srpp = srp->problem;

  if (!tst->nwrun_spool_dir[0]) abort();

  priority += 16;
  if (priority < 0) priority = 0;
  if (priority > 31) priority = 31;

  result->status = RUN_CHECK_FAILED;

  if (os_IsAbsolutePath(tst->nwrun_spool_dir)) {
    snprintf(full_spool_dir, sizeof(full_spool_dir), "%s",
             tst->nwrun_spool_dir);
  } else {
    if (config && config->contests_home_dir) {
      snprintf(full_spool_dir, sizeof(full_spool_dir), "%s/%s",
               config->contests_home_dir, tst->nwrun_spool_dir);
    } else {
#if defined EJUDGE_CONTESTS_HOME_DIR
      snprintf(full_spool_dir, sizeof(full_spool_dir), "%s/%s",
               EJUDGE_CONTESTS_HOME_DIR, tst->nwrun_spool_dir);
#else
      err("cannot initialize full_spool_dir");
      chk_printf(result, "full_spool_dir is invalid\n");
      goto fail;
#endif
    }
  }

  snprintf(queue_path, sizeof(queue_path), "%s/queue",
           full_spool_dir);
  if (make_all_dir(queue_path, 0777) < 0) {
    chk_printf(result, "make_all_dir(%s) failed\n", queue_path);
    goto fail;
  }

  snprintf(pkt_name, sizeof(pkt_name), "%c%c%d%c%d%c%d%c%d%c%d",
           b32_digits[priority],
           get_num_prefix(srgp->contest_id), srgp->contest_id,
           get_num_prefix(srgp->run_id), srgp->run_id,
           get_num_prefix(srpp->id), srpp->id,
           get_num_prefix(test_num), test_num,
           get_num_prefix(srgp->judge_id), srgp->judge_id);
  snprintf(full_in_path, sizeof(full_in_path),
           "%s/in/%s_%s", queue_path, os_NodeName(), pkt_name);
  if (make_dir(full_in_path, 0777) < 0) {
    chk_printf(result, "make_dir(%s) failed\n", full_in_path);
    goto fail;
  }

  // copy (or link) the executable
  snprintf(exe_src_path, sizeof(exe_src_path), "%s/%s",
           exe_src_dir, exe_basename);
  snprintf(tmp_in_path, sizeof(tmp_in_path), "%s/%s",
           full_in_path, exe_basename);
  if (make_hardlink(exe_src_path, tmp_in_path) < 0) {
    chk_printf(result, "copy(%s, %s) failed\n", exe_src_path, tmp_in_path);
    goto fail;
  }

  // copy (or link) the test file
  snprintf(tmp_in_path, sizeof(tmp_in_path), "%s/%s",
           full_in_path, test_basename);
  if (make_hardlink(test_src_path, tmp_in_path) < 0) {
    chk_printf(result, "copy(%s, %s) failed\n", test_src_path, tmp_in_path);
    goto fail;
  }

  // make the description file
  snprintf(tmp_in_path, sizeof(tmp_in_path), "%s/packet.cfg", full_in_path);
  f = fopen(tmp_in_path, "w");
  if (!f) {
    chk_printf(result, "fopen(%s) failed\n", tmp_in_path);
    goto fail;
  }

  fprintf(f, "priority = %d\n", priority + 1);
  fprintf(f, "contest_id = %d\n", srgp->contest_id);
  fprintf(f, "run_id = %d\n", srgp->run_id + 1);
  fprintf(f, "prob_id = %d\n", srpp->id);
  fprintf(f, "test_num = %d\n", test_num);
  fprintf(f, "judge_id = %d\n", srgp->judge_id);
  fprintf(f, "use_contest_id_in_reply = %d\n", 1);
  fprintf(f, "enable_unix2dos = %d\n", 1);
  if (srpp->use_stdin > 0 || srpp->combined_stdin > 0) {
    fprintf(f, "redirect_stdin = %d\n", 1);
  } else {
    fprintf(f, "disable_stdin = %d\n", 1);
  }
  if (srpp->combined_stdin > 0) {
    fprintf(f, "combined_stdin = %d\n", 1);
  }
  if (srpp->use_stdout > 0 || srpp->combined_stdout > 0) {
    fprintf(f, "redirect_stdout = %d\n", 1);
  } else {
    fprintf(f, "ignore_stdout = %d\n", 1);
  }
  if (srpp->combined_stdout > 0) {
    fprintf(f, "combined_stdout = %d\n", 1);
  }
  fprintf(f, "redirect_stderr = %d\n", 1);
  fprintf(f, "time_limit_millis = %ld\n", time_limit_millis);
  if (srpp->real_time_limit_ms > 0) {
    fprintf(f, "real_time_limit_millis = %d\n", srpp->real_time_limit_ms);
  }
  if (srpp->max_stack_size != 0 && srpp->max_stack_size != (size_t) -1L) {
    fprintf(f, "max_stack_size = %" EJ_PRINTF_ZSPEC "u\n",
            EJ_PRINTF_ZCAST(srpp->max_stack_size));
  }
  if (srpp->max_data_size != 0 && srpp->max_data_size != (size_t) -1L) {
    fprintf(f, "max_data_size = %" EJ_PRINTF_ZSPEC "u\n",
            EJ_PRINTF_ZCAST(srpp->max_data_size));
  }
  if (srpp->max_vm_size != 0 && srpp->max_vm_size != (size_t) -1L) {
    fprintf(f, "max_vm_size = %" EJ_PRINTF_ZSPEC "u\n",
            EJ_PRINTF_ZCAST(srpp->max_vm_size));
  }
  fprintf(f, "max_output_file_size = 60M\n");
  fprintf(f, "max_error_file_size = 16M\n");
  if (srgp->secure_run) {
    fprintf(f, "enable_secure_run = 1\n");
  }
  if (srgp->enable_memory_limit_error && srgp->secure_run) {
    fprintf(f, "enable_memory_limit_error = 1\n");
  }
  if (srgp->detect_violations && srgp->secure_run) {
    fprintf(f, "enable_security_violation_error = 1\n");
  }
  fprintf(f, "prob_short_name = \"%s\"\n", srpp->short_name);
  fprintf(f, "program_name = \"%s\"\n", exe_basename);
  fprintf(f, "test_file_name = \"%s\"\n", test_basename);
  fprintf(f, "input_file_name = \"%s\"\n", srpp->input_file);
  fprintf(f, "output_file_name = \"%s\"\n", srpp->output_file);
  fprintf(f, "result_file_name = \"%s\"\n", srpp->output_file);
  fprintf(f, "error_file_name = \"%s\"\n", tst->error_file);
  fprintf(f, "log_file_name = \"%s\"\n", tst->error_file);

  fflush(f);
  if (ferror(f)) {
    chk_printf(result, "output error to %s\n", tmp_in_path);
    goto fail;
  }
  fclose(f); f = 0;

  // wait for the result package
  snprintf(result_path, sizeof(result_path), "%s/result/%06d",
           full_spool_dir, srgp->contest_id);
  make_all_dir(result_path, 0777);

  snprintf(full_dir_path, sizeof(full_dir_path),
           "%s/dir/%s", queue_path, pkt_name);
  if (rename(full_in_path, full_dir_path) < 0) {
    chk_printf(result, "rename(%s, %s) failed\n", full_in_path, full_dir_path);
    goto fail;
  }

 restart_waiting:;

  // wait for the result package
  // timeout is 2 * real_time_limit
  timeout = 0;
  if (srpp->real_time_limit_ms > 0) timeout = 3 * srpp->real_time_limit_ms;
  if (timeout <= 0) timeout = 3 * time_limit_millis;
  wait_time = 0;

  while (1) {
    r = scan_dir(result_path, result_pkt_name, sizeof(result_pkt_name));
    if (r < 0) {
      chk_printf(result, "scan_dir(%s) failed\n", result_path);
      goto fail;
    }

    if (r > 0) break;

    if (wait_time >= timeout) {
      chk_printf(result, "invoke_nwrun: timeout!\n");
      goto fail;
    }

    cr_serialize_unlock(state);
    interrupt_enable();
    os_Sleep(100);
    interrupt_disable();
    cr_serialize_lock(state);

    // more appropriate interval?
    wait_time += 100;
  }

  snprintf(dir_entry_packet, sizeof(dir_entry_packet), "%s/dir/%s",
           result_path, result_pkt_name);
  snprintf(out_entry_packet, sizeof(out_entry_packet), "%s/out/%s_%s",
           result_path, os_NodeName(), result_pkt_name);
  if (rename(dir_entry_packet, out_entry_packet) < 0) {
    err("rename(%s, %s) failed: %s", dir_entry_packet, out_entry_packet,
        os_ErrorMsg());
    chk_printf(result, "rename(%s, %s) failed", dir_entry_packet,
               out_entry_packet);
    goto fail;
  }

  // parse the resulting packet
  snprintf(tmp_in_path, sizeof(tmp_in_path), "%s/packet.cfg",
           out_entry_packet);
  generic_out_packet = nwrun_out_packet_parse(tmp_in_path, &out_packet);
  if (!generic_out_packet) {
    chk_printf(result, "out_packet parse failed for %s\n", tmp_in_path);
    goto fail;
  }

  // match output and input data
  if (out_packet->contest_id != srgp->contest_id) {
    chk_printf(result, "contest_id mismatch: %d, %d\n",
               out_packet->contest_id, srgp->contest_id);
    goto restart_waiting;
  }
  if (out_packet->run_id - 1 != srgp->run_id) {
    chk_printf(result, "run_id mismatch: %d, %d\n",
               out_packet->run_id, srgp->run_id);
    goto restart_waiting;
  }
  if (out_packet->prob_id != srpp->id) {
    chk_printf(result, "prob_id mismatch: %d, %d\n",
               out_packet->prob_id, srpp->id);
    goto restart_waiting;
  }
  if (out_packet->test_num != test_num) {
    chk_printf(result, "test_num mismatch: %d, %d\n",
               out_packet->test_num, test_num);
    goto restart_waiting;
  }
  if (out_packet->judge_id != srgp->judge_id) {
    chk_printf(result, "judge_id mismatch: %d, %d\n",
               out_packet->judge_id, srgp->judge_id);
    goto restart_waiting;
  }

  result->status = out_packet->status;
  if (result->status != RUN_OK
      && result->status != RUN_PRESENTATION_ERR
      && result->status != RUN_RUN_TIME_ERR
      && result->status != RUN_TIME_LIMIT_ERR
      && result->status != RUN_CHECK_FAILED
      && result->status != RUN_MEM_LIMIT_ERR
      && result->status != RUN_SECURITY_ERR) {
    chk_printf(result, "invalid status %d\n", result->status);
    goto fail;
  }

  if (result->status != RUN_OK && out_packet->comment[0]) {
    chk_printf(result, "nwrun: %s\n", out_packet->comment);
  }

  if (out_packet->is_signaled) {
    result->code = 256;
    result->termsig = out_packet->signal_num & 0x7f;
  } else {
    result->code = out_packet->exit_code & 0x7f;
  }

  result->times = out_packet->cpu_time_millis;
  if (out_packet->real_time_available) {
    *p_has_real_time = 1;
    result->real_time = out_packet->real_time_millis;
  }
  if (out_packet->exit_comment[0]) {
    result->exit_comment = xstrdup(out_packet->exit_comment);
  }
  if (out_packet->max_memory_used > 0) {
    result->max_memory_used = out_packet->max_memory_used;
  }

  /* handle the input test data */
  if (srgp->enable_full_archive) {
    filehash_get(test_src_path, result->input_digest);
    result->has_input_digest = 1;
  } else if (srpp->binary_input <= 0) {
    file_size = generic_file_size(0, test_src_path, 0);
    if (file_size >= 0) {
      result->input_size = file_size;
      if (srgp->max_file_length > 0 && file_size <= srgp->max_file_length) {
        if (generic_read_file(&result->input, 0, 0, 0, 0, test_src_path, "")<0){
          chk_printf(result, "generic_read_file(%s) failed\n", test_src_path);
          goto fail;
        }
      }
    }
  }

  /* handle the program output */
  if (out_packet->output_file_existed > 0
      && out_packet->output_file_too_big <= 0) {
    snprintf(packet_output_path, sizeof(packet_output_path),
             "%s/%s", out_entry_packet, srpp->output_file);
    if (result->status == RUN_OK) {
      // copy file into the working directory for further checking
      snprintf(check_output_path, sizeof(check_output_path),
               "%s/%s", tst->check_dir, srpp->output_file);
      if (fast_copy_file(packet_output_path, check_output_path) < 0) {
        chk_printf(result, "copy_file(%s, %s) failed\n",
                   packet_output_path, check_output_path);
        goto fail;
      }
    }

    result->output_size = out_packet->output_file_orig_size;
    if (!srgp->enable_full_archive
        && srpp->binary_input <= 0
        && srgp->max_file_length > 0
        && result->output_size <= srgp->max_file_length) {
      if (generic_read_file(&result->output,0,0,0,0,packet_output_path,"")<0) {
        chk_printf(result, "generic_read_file(%s) failed\n",
                   packet_output_path);
        goto fail;
      }
    }

    if (far) {
      snprintf(arch_entry_name, sizeof(arch_entry_name), "%06d.o", test_num);
      full_archive_append_file(far, arch_entry_name, 0, packet_output_path);
    }
  } else if (out_packet->output_file_existed > 0) {
    chk_printf(result, "output file is too big\n");
  }

  /* handle the program error file */
  if (out_packet->error_file_existed > 0) {
    snprintf(packet_error_path, sizeof(packet_error_path),
             "%s/%s", out_entry_packet, tst->error_file);
    result->error_size = out_packet->error_file_size;
    if (!srgp->enable_full_archive
        && srgp->max_file_length > 0
        && result->error_size <= srgp->max_file_length) {
      if (generic_read_file(&result->error,0,0,0,0,packet_error_path,"") < 0) {
        chk_printf(result, "generic_read_file(%s) failed\n",
                   packet_error_path);
        goto fail;
      }
    }
    if (far) {
      snprintf(arch_entry_name, sizeof(arch_entry_name), "%06d.e", test_num);
      full_archive_append_file(far, arch_entry_name, 0, packet_error_path);
    }
  }

 cleanup:
  if (out_entry_packet[0]) {
    remove_directory_recursively(out_entry_packet, 0);
  }
  if (f) fclose(f);
  generic_out_packet = nwrun_out_packet_free(generic_out_packet);
  return result->status;

 fail:
  result->status = RUN_CHECK_FAILED;
  goto cleanup;
}

static int
invoke_tar(
        const unsigned char *tar_path,
        const unsigned char *archive_path,
        const unsigned char *working_dir,
        const unsigned char *report_path)
{
  tpTask tsk = NULL;
  int retval = -1;

  info("starting: %s", tar_path);
  tsk = task_New();
  task_AddArg(tsk, tar_path);
  task_AddArg(tsk, "xfz");
  task_AddArg(tsk, archive_path);
  task_SetPathAsArg0(tsk);
  task_SetWorkingDir(tsk, working_dir);
  task_SetRedir(tsk, 0, TSR_FILE, "/dev/null", TSK_READ, 0);
  task_SetRedir(tsk, 1, TSR_FILE, report_path, TSK_REWRITE, TSK_FULL_RW);
  task_SetRedir(tsk, 2, TSR_DUP, 1);
  task_Start(tsk);
  task_Wait(tsk);
  if (task_IsAbnormal(tsk)) {
    goto cleanup;
  }

  retval = 0;

cleanup:
  task_Delete(tsk); tsk = 0;
  return retval;
}

static tpTask
invoke_interactor(
        const unsigned char *interactor_cmd,
        const unsigned char *test_src_path,
        const unsigned char *output_path,
        const unsigned char *corr_src_path,
        const unsigned char *working_dir,
        const unsigned char *check_out_path,
        char **interactor_env,
        int stdin_fd,
        int stdout_fd,
        long time_limit_ms)
{
  tpTask tsk_int = NULL;
  unsigned char ejudge_prefix_dir_env[PATH_MAX];

  ejudge_prefix_dir_env[0] = 0;
#ifdef EJUDGE_PREFIX_DIR
  snprintf(ejudge_prefix_dir_env, sizeof(ejudge_prefix_dir_env), "EJUDGE_PREFIX_DIR=%s", EJUDGE_PREFIX_DIR);
#endif /* EJUDGE_PREFIX_DIR */

  tsk_int = task_New();
  task_AddArg(tsk_int, interactor_cmd);
  task_AddArg(tsk_int, test_src_path);
  task_AddArg(tsk_int, output_path);
  if (corr_src_path && corr_src_path[0]) {
    task_AddArg(tsk_int, corr_src_path);
  }
  task_SetPathAsArg0(tsk_int);
  task_SetWorkingDir(tsk_int, working_dir);
  setup_environment(tsk_int, interactor_env, ejudge_prefix_dir_env, NULL);
  task_SetRedir(tsk_int, 0, TSR_DUP, stdin_fd);
  task_SetRedir(tsk_int, 1, TSR_DUP, stdout_fd);
  task_SetRedir(tsk_int, 2, TSR_FILE, check_out_path, TSK_REWRITE, TSK_FULL_RW);
  task_EnableAllSignals(tsk_int);
  if (time_limit_ms > 0) {
    task_SetMaxTimeMillis(tsk_int, time_limit_ms);
  }

  task_PrintArgs(tsk_int);

  if (task_Start(tsk_int) < 0) {
    task_Delete(tsk_int);
    tsk_int = NULL;
  }

  return tsk_int;
}

static int
touch_file(const unsigned char *path)
{
  int tmpfd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (tmpfd < 0) return -1;
  close(tmpfd);
  return 0;
}

static void
make_java_limits(unsigned char *buf, int blen, size_t max_vm_size, size_t max_stack_size)
{
  unsigned char bv[1024], bs[1024];

  if (max_vm_size == (ssize_t) -1) max_vm_size = 0;
  if (max_stack_size == (ssize_t) -1) max_stack_size = 0;
  buf[0] = 0;
  if (max_vm_size && max_stack_size) {
    snprintf(buf, blen, "EJUDGE_JAVA_FLAGS=-Xmx%s -Xss%s",
             size_t_to_size(bv, sizeof(bv), max_vm_size),
             size_t_to_size(bs, sizeof(bs), max_stack_size));
  } else if (max_vm_size) {
    snprintf(buf, blen, "EJUDGE_JAVA_FLAGS=-Xmx%s",
             size_t_to_size(bv, sizeof(bv), max_vm_size));
  } else if (max_stack_size) {
    snprintf(buf, blen, "EJUDGE_JAVA_FLAGS=-Xss%s",
             size_t_to_size(bs, sizeof(bs), max_stack_size));
  } else {
  }
}

static unsigned char *
report_args_and_env(testinfo_t *ti)
{
  int i;
  int cmd_args_len = 0;
  unsigned char *s, *args = NULL;

  if (!ti || ti->cmd_argc <= 0) return NULL;

  for (i = 0; i < ti->cmd_argc; i++) {
    cmd_args_len += 16;
    if (ti->cmd_argv[i]) {
      cmd_args_len += strlen(ti->cmd_argv[i] + 16);
    }
  }
  if (cmd_args_len > 0) {
    s = args = (unsigned char *) xmalloc(cmd_args_len + 1);
    for (i = 0; i < ti->cmd_argc; i++) {
      if (ti->cmd_argv[i]) {
        s += sprintf(s, "[%3d]: >%s<\n", i + 1, ti->cmd_argv[i]);
      } else {
        s += sprintf(s, "[%3d]: NULL\n", i + 1);
      }
    }
  }
  return args;
}

static int
invoke_checker(
        const struct super_run_in_global_packet *srgp,
        const struct super_run_in_problem_packet *srpp,
        int cur_test,
        struct testinfo *cur_info,
        const unsigned char *check_cmd,
        const unsigned char *test_src,
        const unsigned char *output_path,
        const unsigned char *corr_src,
        const unsigned char *info_src,
        const unsigned char *tgzdir_src,
        const unsigned char *working_dir,
        const unsigned char *score_out_path,
        const unsigned char *check_out_path,
        const unsigned char *check_dir,
        const unsigned char *ejudge_prefix_dir_env,
        int test_score_count,
        const int *test_score_val)
{
  tpTask tsk = NULL;
  int write_log_mode = TSK_REWRITE;
  int status = RUN_CHECK_FAILED;
  int test_max_score = -1;

  tsk = task_New();
  task_AddArg(tsk, check_cmd);
  task_SetPathAsArg0(tsk);

  task_AddArg(tsk, test_src);
  task_AddArg(tsk, output_path);
  if (srpp->use_corr) {
    task_AddArg(tsk, corr_src);
  }
  if (srpp->use_info) {
    task_AddArg(tsk, info_src);
  }
  if (srpp->use_tgz) {
    task_AddArg(tsk, tgzdir_src);
    task_AddArg(tsk, working_dir);
  }

  if (srpp->interactor_cmd && srpp->interactor_cmd[0]) {
    write_log_mode = TSK_APPEND;
  }

  task_SetRedir(tsk, 0, TSR_FILE, "/dev/null", TSK_READ);
  if (srpp->scoring_checker > 0) {
    task_SetRedir(tsk, 1, TSR_FILE, score_out_path, TSK_REWRITE, TSK_FULL_RW);
    task_SetRedir(tsk, 2, TSR_FILE, check_out_path, write_log_mode, TSK_FULL_RW);
  } else {
    task_SetRedir(tsk, 1, TSR_FILE, check_out_path, write_log_mode, TSK_FULL_RW);
    task_SetRedir(tsk, 2, TSR_DUP, 1);
  }

  task_SetWorkingDir(tsk, check_dir);
  if (srpp->checker_real_time_limit_ms > 0) {
    task_SetMaxRealTimeMillis(tsk, srpp->checker_real_time_limit_ms);
  }
  setup_environment(tsk, srpp->checker_env, ejudge_prefix_dir_env, NULL);
  task_EnableAllSignals(tsk);

  task_PrintArgs(tsk);

  if (task_Start(tsk) < 0) {
    append_msg_to_log(check_out_path, "failed to start checker %s", check_cmd);
    status = RUN_CHECK_FAILED;
    goto cleanup;
  }

  task_Wait(tsk);
  task_Log(tsk, 0, LOG_INFO);

  if (task_IsTimeout(tsk)) {
    append_msg_to_log(check_out_path, "checker timeout (%ld ms)", task_GetRunningTime(tsk));
    err("checker timeout (%ld ms)", task_GetRunningTime(tsk));
    status = RUN_CHECK_FAILED;
    goto cleanup;
  }

  if (task_Status(tsk) == TSK_SIGNALED) {
    int signo = task_TermSignal(tsk);
    append_msg_to_log(check_out_path, "checker terminated with signal %d (%s)",
                      signo, os_GetSignalString(signo));
    status = RUN_CHECK_FAILED;
    goto cleanup;
  }

  int exitcode = task_ExitCode(tsk);
  if (exitcode != RUN_OK && exitcode != RUN_PRESENTATION_ERR
      && exitcode != RUN_WRONG_ANSWER_ERR && exitcode != RUN_CHECK_FAILED) {
    append_msg_to_log(check_out_path, "checker exited with code %d", exitcode);
    status = RUN_CHECK_FAILED;
    goto cleanup;
  }

  status = exitcode;
  if (status == RUN_CHECK_FAILED || status == RUN_PRESENTATION_ERR) {
    goto cleanup;
  }

  if (srpp->scoring_checker) {
    switch (srgp->scoring_system_val) {
    case SCORE_KIROV:
    case SCORE_OLYMPIAD:
      test_max_score = -1;
      if (test_score_val && cur_test > 0 && cur_test < test_score_count) {
        test_max_score = test_score_val[cur_test];
      }
      if (test_max_score < 0) {
        test_max_score = srpp->test_score;
      }
      if (test_max_score < 0) test_max_score = 0;
      break;
    case SCORE_MOSCOW:
      test_max_score = srpp->full_score - 1;
      break;
    case SCORE_ACM:
      test_max_score = 0;
      break;
    default:
      abort();
    }
    if (read_checker_score(score_out_path, check_out_path, "checker",
                           test_max_score, &cur_info->checker_score) < 0) {
      status = RUN_CHECK_FAILED;
      goto cleanup;
    }
  }

cleanup:
  task_Delete(tsk); tsk = NULL;
  return status;
}

static int
run_one_test(
        const struct ejudge_cfg *config,
        serve_state_t state,
        const struct super_run_in_packet *srp,
        const struct section_tester_data *tst,
        int cur_test,
        struct testinfo_vector *tests,
        full_archive_t far,
        const unsigned char *exe_name,
        const unsigned char *report_path,
        const unsigned char *check_cmd,
        int open_tests_count,
        const int *open_tests_val,
        int test_score_count,
        const int *test_score_val,
        long long expected_free_space,
        int *p_has_real_time,
        int *p_has_max_memory_used,
        long *p_report_time_limit_ms,
        long *p_report_real_time_limit_ms)
{
  const struct section_global_data *global = state->global;

  const struct super_run_in_global_packet *srgp = srp->global;
  const struct super_run_in_problem_packet *srpp = srp->problem;

  unsigned char ejudge_prefix_dir_env[PATH_MAX];

  unsigned char test_base[PATH_MAX];
  unsigned char corr_base[PATH_MAX];
  unsigned char info_base[PATH_MAX];
  unsigned char tgz_base[PATH_MAX];
  unsigned char tgzdir_base[PATH_MAX];

  unsigned char test_src[PATH_MAX];
  unsigned char corr_src[PATH_MAX];
  unsigned char info_src[PATH_MAX];
  unsigned char tgz_src[PATH_MAX];
  unsigned char tgzdir_src[PATH_MAX];

  unsigned char check_out_path[PATH_MAX];
  unsigned char score_out_path[PATH_MAX];
  const unsigned char *output_path_to_check = NULL;

  unsigned char check_dir[PATH_MAX];
  unsigned char exe_path[PATH_MAX];
  unsigned char working_dir[PATH_MAX];
  unsigned char input_path[PATH_MAX];
  unsigned char output_path[PATH_MAX];
  unsigned char error_path[PATH_MAX];
  unsigned char arg0_path[PATH_MAX];
  unsigned char error_file[PATH_MAX];
  unsigned char error_code[PATH_MAX];
  unsigned char arch_entry_name[PATH_MAX];

  unsigned char mem_limit_buf[PATH_MAX];

  struct testinfo *cur_info = NULL;
  int time_limit_value_ms = 0;
  int status = RUN_CHECK_FAILED;
  int errcode = 0;
  int disable_stderr = -1;
  int copy_flag = 0;
  int error_code_value = 0;
  long long file_size;

  int pfd1[2] = { -1, -1 };
  int pfd2[2] = { -1, -1 };
  testinfo_t tstinfo;
  tpTask tsk_int = NULL;
  tpTask tsk = NULL;

#ifdef HAVE_TERMIOS_H
  struct termios term_attrs;
#endif

  memset(&tstinfo, 0, sizeof(tstinfo));

#ifdef HAVE_TERMIOS_H
  memset(&term_attrs, 0, sizeof(term_attrs));
#endif

  if (srgp->scoring_system_val == SCORE_OLYMPIAD
      && srgp->accepting_mode > 0
      && cur_test > srpp->tests_to_accept) {
    return -1;
  }

  ejudge_prefix_dir_env[0] = 0;
#ifdef EJUDGE_PREFIX_DIR
  snprintf(ejudge_prefix_dir_env, sizeof(ejudge_prefix_dir_env),
           "EJUDGE_PREFIX_DIR=%s", EJUDGE_PREFIX_DIR);
#endif /* EJUDGE_PREFIX_DIR */

  test_base[0] = 0;
  test_src[0] = 0;
  if (srpp->test_pat && srpp->test_pat[0]) {
    snprintf(test_base, sizeof(test_base), srpp->test_pat, cur_test);
    snprintf(test_src, sizeof(test_src), "%s/%s", srpp->test_dir, test_base);
  }
  corr_base[0] = 0;
  corr_src[0] = 0;
  if (srpp->corr_pat && srpp->corr_pat[0]) {
    snprintf(corr_base, sizeof(corr_base), srpp->corr_pat, cur_test);
    snprintf(corr_src, sizeof(corr_src), "%s/%s", srpp->corr_dir, corr_base);
  }
  info_base[0] = 0;
  info_src[0] = 0;
  if (srpp->use_info > 0) {
    snprintf(info_base, sizeof(info_base), srpp->info_pat, cur_test);
    snprintf(info_src, sizeof(info_src), "%s/%s", srpp->info_dir, info_base);
  }
  tgz_base[0] = 0;
  tgzdir_base[0] = 0;
  tgz_src[0] = 0;
  tgzdir_src[0] = 0;
  if (srpp->use_tgz > 0) {
    snprintf(tgz_base, sizeof(tgz_base), srpp->tgz_pat, cur_test);
    snprintf(tgz_src, sizeof(tgz_src), "%s/%s", srpp->tgz_dir, tgz_base);
    snprintf(tgzdir_base, sizeof(tgzdir_base), srpp->tgzdir_pat, cur_test);
    snprintf(tgzdir_src, sizeof(tgzdir_src), "%s/%s", srpp->tgz_dir, tgzdir_base);
  }

  if (os_CheckAccess(test_src, REUSE_R_OK) < 0) {
    return -1;
  }

  if (tst && tst->check_dir && tst->check_dir[0]) {
    snprintf(check_dir, sizeof(check_dir), "%s", tst->check_dir);
  } else {
    snprintf(check_dir, sizeof(check_dir), "%s", global->run_check_dir);
  }

  ASSERT(cur_test == tests->size);

  if (tests->size >= tests->reserved) {
    tests->reserved *= 2;
    if (!tests->reserved) tests->reserved = 32;
    tests->data = (typeof(tests->data)) xrealloc(tests->data, tests->reserved * sizeof(tests->data[0]));
  }
  memset(&tests->data[cur_test], 0, sizeof(tests->data[0]));
  cur_info = &tests->data[cur_test];
  ++tests->size;

  cur_info->input_size = -1;
  cur_info->output_size = -1;
  cur_info->error_size = -1;
  cur_info->correct_size = -1;
  cur_info->chk_out_size = -1;
  cur_info->visibility = TV_NORMAL;
  if (open_tests_val && cur_test > 0 && cur_test < open_tests_count) {
    cur_info->visibility = open_tests_val[cur_test];
  }

  time_limit_value_ms = 0;
  if (srpp->time_limit_ms > 0) {
    time_limit_value_ms += srpp->time_limit_ms;
  }
  if (time_limit_value_ms > 0) {
    // adjustment works only for limited time
    if (tst && tst->time_limit_adj_millis > 0)
      time_limit_value_ms += tst->time_limit_adj_millis;
    else if (tst && tst->time_limit_adjustment > 0)
      time_limit_value_ms += tst->time_limit_adjustment * 1000;
    if (srgp->lang_time_limit_adj_ms > 0)
      time_limit_value_ms += srgp->lang_time_limit_adj_ms;
  }

  snprintf(check_out_path, sizeof(check_out_path), "%s/checkout_%d.txt",
           global->run_work_dir, cur_test);
  snprintf(score_out_path, sizeof(score_out_path), "%s/scoreout_%d.txt",
           global->run_work_dir, cur_test);

  error_code[0] = 0;
  if (tst && tst->errorcode_file[0]) {
    snprintf(error_code, sizeof(error_code), "%s/%s", check_dir, tst->errorcode_file);
  }

  if (tst && tst->nwrun_spool_dir && tst->nwrun_spool_dir[0]) {
    status = invoke_nwrun(config, state,
                          tst, srp, far,
                          cur_test, 0, p_has_real_time,
                          global->run_work_dir,
                          exe_name, test_src, test_base, time_limit_value_ms, cur_info);
    if (cur_info->max_memory_used > 0) {
      *p_has_max_memory_used = 1;
    }
    if (status > 0) {
      goto cleanup;
    }
    goto run_checker;
  }

  /* Load test information file */
  if (srpp->use_info > 0) {
    if ((errcode = testinfo_parse(info_src, &tstinfo)) < 0) {
      err("Cannot parse test info file '%s': %s", info_src, testinfo_strerror(-errcode));
      append_msg_to_log(check_out_path, "failed to parse testinfo file '%s': %s\n",
                        info_src, testinfo_strerror(-errcode));
      goto check_failed;
    }
  }

  if (srpp->use_info > 0 && tstinfo.disable_stderr >= 0) {
    disable_stderr = tstinfo.disable_stderr;
  }
  if (disable_stderr < 0) {
    disable_stderr = srpp->disable_stderr;
  }
  if (disable_stderr < 0) {
    disable_stderr = 0;
  }

  make_writable(check_dir);
  clear_directory(check_dir);
  check_free_space(check_dir, expected_free_space);

  if (generic_copy_file(0, global->run_work_dir, exe_name, "", 0, check_dir, exe_name, "") < 0) {
    append_msg_to_log(check_out_path, "failed to copy %s/%s -> %s/%s", global->run_work_dir, exe_name,
                      check_dir, exe_name);
    goto check_failed;
  }

  snprintf(exe_path, sizeof(exe_path), "%s/%s", check_dir, exe_name);
  make_executable(exe_path);

  if (srpp->use_tgz > 0) {
#ifdef __WIN32__
    snprintf(arg0_path, sizeof(arg0_path), "%s%s..%s%s", check_dir, CONF_DIRSEP, CONF_DIRSEP, exe_name);
#else
    snprintf(arg0_path, sizeof(arg0_path), "../%s", exe_name);
#endif
  } else {
#ifdef __WIN32__
    snprintf(arg0_path, sizeof(arg0_path), "%s", exe_path);
#else
    snprintf(arg0_path, sizeof(arg0_path), "./%s", exe_name);
#endif
  }

  if (srpp->use_tgz > 0) {
    snprintf(working_dir, sizeof(working_dir), "%s/%s", check_dir, tgzdir_base);
  } else {
    snprintf(working_dir, sizeof(working_dir), "%s", check_dir);
  }

  if (srpp->use_tgz > 0) {
    if (invoke_tar("/bin/tar", tgz_src, check_dir, report_path) < 0) {
      goto check_failed;
    }
  }

  if (tst && tst->is_dos && !srpp->binary_input) copy_flag = CONVERT;

  /* copy the test */
  if (generic_copy_file(0, NULL, test_src, "", copy_flag, check_dir, srpp->input_file, "") < 0) {
    append_msg_to_log(check_out_path, "failed to copy test file %s -> %s/%s",
                      test_src, check_dir, srpp->input_file);
    goto check_failed;
  }

  if (tst && tst->error_file && tst->error_file[0]) {
    snprintf(error_file, sizeof(error_file), "%s", tst->error_file);
  } else {
    snprintf(error_file, sizeof(error_file), "%s", "error");
  }

  snprintf(input_path, sizeof(input_path), "%s/%s", check_dir, srpp->input_file);
  snprintf(output_path, sizeof(output_path), "%s/%s", check_dir, srpp->output_file);
  snprintf(error_path, sizeof(error_path), "%s/%s", check_dir, error_file);

  if (srpp->interactor_cmd && srpp->interactor_cmd[0]) {
    snprintf(output_path, sizeof(output_path), "%s/%s", global->run_work_dir, srpp->output_file);
  }

  // FIXME: handle output-only problem somewhere else

#ifndef __WIN32__
  if (srpp->interactor_cmd && srpp->interactor_cmd[0]) {
    if (pipe(pfd1) < 0) {
      append_msg_to_log(check_out_path, "pipe() failed: %s", os_ErrorMsg());
      goto check_failed;
    }
    fcntl(pfd1[0], F_SETFD, FD_CLOEXEC);
    fcntl(pfd1[1], F_SETFD, FD_CLOEXEC);
    if (pipe(pfd2) < 0) {
      append_msg_to_log(check_out_path, "pipe() failed: %s", os_ErrorMsg());
      goto check_failed;
    }
    fcntl(pfd2[0], F_SETFD, FD_CLOEXEC);
    fcntl(pfd2[1], F_SETFD, FD_CLOEXEC);

    tsk_int = invoke_interactor(srpp->interactor_cmd, test_src, output_path, corr_src,
                                working_dir, check_out_path, srpp->interactor_env,
                                pfd1[0], pfd2[1], srpp->interactor_time_limit_ms);
    if (!tsk_int) {
      append_msg_to_log(check_out_path, "interactor failed to start");
      goto check_failed;
    }
  }
#endif

  tsk = task_New();
  if (tst && tst->start_cmd && tst->start_cmd[0]) {
    info("starting: %s %s", tst->start_cmd, arg0_path);
    task_AddArg(tsk, tst->start_cmd);
    if (srpp->input_file && srpp->input_file[0]) {
      task_SetEnv(tsk, "INPUT_FILE", srpp->input_file);
    }
    if (srpp->output_file && srpp->output_file[0]) {
      task_SetEnv(tsk, "OUTPUT_FILE", srpp->output_file);
    }
  } else {
    info("starting: %s", arg0_path);
  }

  task_AddArg(tsk, arg0_path);
  if (srpp->use_info > 0 && tstinfo.cmd_argc >= 1) {
    task_pnAddArgs(tsk, tstinfo.cmd_argc, (char**) tstinfo.cmd_argv);
  }
  task_SetPathAsArg0(tsk);
  task_SetWorkingDir(tsk, working_dir);

  if (srpp->interactor_cmd && srpp->interactor_cmd[0]) {
    task_SetRedir(tsk, 0, TSR_DUP, pfd2[0]);
    task_SetRedir(tsk, 1, TSR_DUP, pfd1[1]);
    if (tst->ignore_stderr > 0) {
      task_SetRedir(tsk, 2, TSR_FILE, "/dev/null", TSK_WRITE, TSK_FULL_RW);
    } else {
      task_SetRedir(tsk, 2, TSR_FILE, error_path, TSK_REWRITE, TSK_FULL_RW);
    }
  } else if (tst && tst->no_redirect > 0) {
    task_SetRedir(tsk, 0, TSR_FILE, "/dev/null", TSK_READ);
    task_SetRedir(tsk, 1, TSR_FILE, "/dev/null", TSK_WRITE, TSK_FULL_RW);
    task_SetRedir(tsk, 2, TSR_FILE, "/dev/null", TSK_WRITE, TSK_FULL_RW);
    touch_file(output_path);
  } else {
    if (srpp->use_stdin > 0) {
      task_SetRedir(tsk, 0, TSR_FILE, input_path, TSK_READ);
    } else {
      task_SetRedir(tsk, 0, TSR_FILE, "/dev/null", TSK_READ);
    }
    if (srpp->use_stdout > 0 && srpp->use_info > 0 && tstinfo.check_stderr) {
      task_SetRedir(tsk, 1, TSR_FILE, "/dev/null", TSK_WRITE, TSK_FULL_RW);
      task_SetRedir(tsk, 2, TSR_FILE, output_path, TSK_REWRITE, TSK_FULL_RW);
      touch_file(output_path);
    } else {
      if (srpp->use_stdout > 0) {
        task_SetRedir(tsk, 1, TSR_FILE, output_path, TSK_REWRITE, TSK_FULL_RW);
      } else {
        task_SetRedir(tsk, 1, TSR_FILE, "/dev/null", TSK_WRITE, TSK_FULL_RW);
      }
      touch_file(output_path);
      if (tst && tst->ignore_stderr > 0 && disable_stderr <= 0) {
        task_SetRedir(tsk, 2, TSR_FILE, "/dev/null", TSK_WRITE, TSK_FULL_RW);
      } else {
        task_SetRedir(tsk, 2, TSR_FILE, error_path, TSK_REWRITE, TSK_FULL_RW);
        touch_file(error_path);
      }
    }
  }

  if (tst && tst->clear_env) task_ClearEnv(tsk);
  setup_environment(tsk, tst->start_env, ejudge_prefix_dir_env, &tstinfo);

  if (time_limit_value_ms > 0) {
    if ((time_limit_value_ms % 1000)) {
      task_SetMaxTimeMillis(tsk, time_limit_value_ms);
    } else {
      task_SetMaxTime(tsk, time_limit_value_ms / 1000);
    }
    *p_report_time_limit_ms = time_limit_value_ms;
  }

  if (srpp->real_time_limit_ms > 0) {
    task_SetMaxRealTimeMillis(tsk, srpp->real_time_limit_ms);
    *p_report_real_time_limit_ms = srpp->real_time_limit_ms;
  }

  if (tst && tst->kill_signal && tst->kill_signal[0]) task_SetKillSignal(tsk, tst->kill_signal);
  if (tst && tst->no_core_dump) task_DisableCoreDump(tsk);

  if (!tst || tst->memory_limit_type_val < 0) {
    if (srpp->max_stack_size && srpp->max_stack_size != (ssize_t) -1L)
      task_SetStackSize(tsk, srpp->max_stack_size);
    if (srpp->max_data_size && srpp->max_data_size != (ssize_t) -1L)
      task_SetDataSize(tsk, srpp->max_data_size);
    if (srpp->max_vm_size && srpp->max_vm_size != (ssize_t) -1L)
      task_SetVMSize(tsk, srpp->max_vm_size);
  } else {
    switch (tst->memory_limit_type_val) {
    case MEMLIMIT_TYPE_DEFAULT:
      if (srpp->max_stack_size && srpp->max_stack_size != (ssize_t) -1L)
        task_SetStackSize(tsk, srpp->max_stack_size);
      if (srpp->max_data_size && srpp->max_data_size != (ssize_t) -1L)
        task_SetDataSize(tsk, srpp->max_data_size);
      if (srpp->max_vm_size && srpp->max_vm_size != (ssize_t) -1L)
        task_SetVMSize(tsk, srpp->max_vm_size);
      if (tst->enable_memory_limit_error && srgp->enable_memory_limit_error && srgp->secure_run) {
        task_EnableMemoryLimitError(tsk);
      }
      break;
    case MEMLIMIT_TYPE_JAVA:
      make_java_limits(mem_limit_buf, sizeof(mem_limit_buf), srpp->max_vm_size, srpp->max_stack_size);
      if (mem_limit_buf[0]) {
        task_PutEnv(tsk, mem_limit_buf);
      }
      break;
    case MEMLIMIT_TYPE_DOS:
      break;
    default:
      abort();
    }
  }

  if (tst && tst->secure_exec_type_val > 0 && srgp->secure_run) {
    switch (tst->secure_exec_type_val) {
    case SEXEC_TYPE_STATIC:
      if (task_EnableSecureExec(tsk) < 0) {
        err("task_EnableSecureExec() failed");
        append_msg_to_log(check_out_path, "task_EnableSecureExec() failed");
        goto check_failed;
      }
      break;
    case SEXEC_TYPE_DLL:
      task_PutEnv(tsk, "LD_BIND_NOW=1");
      task_FormatEnv(tsk, "LD_PRELOAD", "%s/lang/libdropcaps.so", EJUDGE_SCRIPT_DIR);
      break;
    case SEXEC_TYPE_DLL32:
      task_PutEnv(tsk, "LD_BIND_NOW=1");
      task_FormatEnv(tsk, "LD_PRELOAD", "%s/lang/libdropcaps32.so", EJUDGE_SCRIPT_DIR);
      break;
    case SEXEC_TYPE_JAVA:
      task_PutEnv(tsk, "EJUDGE_JAVA_POLICY=fileio.policy");
      break;
    default:
      abort();
    }
  }

  if (tst && tst->enable_memory_limit_error && srgp->secure_run && srgp->detect_violations) {
    switch (tst->secure_exec_type_val) {
    case SEXEC_TYPE_STATIC:
    case SEXEC_TYPE_DLL:
    case SEXEC_TYPE_DLL32:
      task_EnableSecurityViolationError(tsk);
      break;
    }
  }

#ifdef HAVE_TERMIOS_H
  if (tst && tst->no_redirect && isatty(0)) {
    /* we need to save terminal state since if the program
     * is killed with SIGKILL, the terminal left in random state
     */
    if (tcgetattr(0, &term_attrs) < 0) {
      err("tcgetattr failed: %s", os_ErrorMsg());
    }
  }
#endif

  task_EnableAllSignals(tsk);

  if (srpp->max_core_size && srpp->max_core_size != (ssize_t) -1L) {
    task_SetMaxCoreSize(tsk, srpp->max_core_size);
  }
  if (srpp->max_file_size && srpp->max_file_size != (ssize_t) -1L) {
    task_SetMaxFileSize(tsk, srpp->max_file_size);
  }
  if (srpp->max_open_file_count > 0) {
    task_SetMaxOpenFileCount(tsk, srpp->max_open_file_count);
  }
  if (srpp->max_process_count > 0) {
    task_SetMaxProcessCount(tsk, srpp->max_process_count);
  }

  //task_PrintArgs(tsk);

  if (task_Start(tsk) < 0) {
    /* failed to start task */
    cur_info->code = task_ErrorCode(tsk, 0, 0);
    append_msg_to_log(check_out_path, "failed to start %s", exe_path);
    goto check_failed;
  }

  if (pfd1[0] >= 0) close(pfd1[0]);
  if (pfd1[1] >= 0) close(pfd1[1]);
  if (pfd2[0] >= 0) close(pfd2[0]);
  if (pfd2[1] >= 0) close(pfd2[1]);
  pfd1[0] = pfd1[1] = pfd2[0] = pfd2[1] = -1;

  task_Wait(tsk);

  info("CPU time = %ld, real time = %ld",
       (long) task_GetRunningTime(tsk), (long) task_GetRealTime(tsk));

  if (error_code[0]) {
    error_code_value = read_error_code(error_code);
  }

  /* restore the terminal state */
#ifdef HAVE_TERMIOS_H
  if (tst && tst->no_redirect && isatty(0)) {
    if (tcsetattr(0, TCSADRAIN, &term_attrs) < 0)
      err("tcsetattr failed: %s", os_ErrorMsg());
  }
#endif

  if (tsk_int) task_Wait(tsk_int);

  /* set normal permissions for the working directory */
  make_writable(check_dir);
  /* make the output file readable */
  if (chmod(output_path, 0600) < 0) {
    err("chmod failed: %s", os_ErrorMsg());
  }
  
  /* fill test report structure */
  cur_info->times = task_GetRunningTime(tsk);
  *p_has_real_time = 1;
  cur_info->real_time = task_GetRealTime(tsk);

  // input file
  file_size = -1;
  if (srgp->enable_full_archive) {
    filehash_get(test_src, cur_info->input_digest);
    cur_info->has_input_digest = 1;
  } else {
    if (srpp->binary_input <= 0) {
      file_size = generic_file_size(0, test_src, 0);
    }
    if (file_size >= 0) {
      cur_info->input_size = file_size;
      if (srgp->max_file_length > 0 && file_size <= srgp->max_file_length) {
        generic_read_file(&cur_info->input, 0, 0, 0, 0, test_src, "");
      }
    }
  }

  // output file
  file_size = -1;
  if (srpp->binary_input <= 0) {
    file_size = generic_file_size(0, output_path, 0);
  }
  if (file_size >= 0) {
    cur_info->output_size = file_size;
    if (srgp->max_file_length > 0 && !srgp->enable_full_archive && file_size <= srgp->max_file_length) {
      generic_read_file(&cur_info->output, 0, 0, 0, 0, output_path, "");
    }
    if (far) {
      snprintf(arch_entry_name, sizeof(arch_entry_name), "%06d.o", cur_test);
      full_archive_append_file(far, arch_entry_name, 0, output_path);
    }
  }

  // error file
  file_size = -1;
  if (error_path[0]) {
    file_size = generic_file_size(0, error_path, 0);
  }
  if (file_size >= 0) {
    cur_info->error_size = file_size;
    if (srgp->max_file_length > 0 && !srgp->enable_full_archive && file_size <= srgp->max_file_length) {
      generic_read_file(&cur_info->error, 0, 0, 0, 0, error_path, "");
    }
    if (far) {
      snprintf(arch_entry_name, sizeof(arch_entry_name), "%06d.e", cur_test);
      full_archive_append_file(far, arch_entry_name, 0, error_path);
    }
  }

  // command-line arguments and environment
  if (srpp->use_info) {
    if (srgp->enable_full_archive) {
      filehash_get(info_src, cur_info->info_digest);
      cur_info->has_info_digest = 1;
    }
    cur_info->args = report_args_and_env(&tstinfo);
    if (tstinfo.comment) {
      cur_info->comment = xstrdup(tstinfo.comment);
    }
    if (tstinfo.team_comment) {
      cur_info->team_comment = xstrdup(tstinfo.team_comment);
    }
  }

  if (tsk_int) {
    if (task_IsTimeout(tsk_int)) {
      append_msg_to_log(check_out_path, "interactor timeout");
      err("interactor timeout");
      goto check_failed;
    }
    if (task_Status(tsk_int) == TSK_SIGNALED) {
      int signo = task_TermSignal(tsk_int);
      task_Log(tsk_int, 0, LOG_INFO);
      append_msg_to_log(check_out_path, "interactor terminated with signal %d (%s)", signo, os_GetSignalString(signo));
      goto check_failed;
    }
    int exitcode = task_ExitCode(tsk_int);
    if (exitcode != RUN_OK && exitcode != RUN_PRESENTATION_ERR && exitcode != RUN_WRONG_ANSWER_ERR) {
      append_msg_to_log(check_out_path, "interactor exited with code %d", exitcode);
      goto check_failed;
    }
  }

  if (task_IsTimeout(tsk)) {
    status = RUN_TIME_LIMIT_ERR;
    goto cleanup;
  }

  if (tst && tst->enable_memory_limit_error && srgp->enable_memory_limit_error
      && srgp->secure_run && task_IsMemoryLimit(tsk)) {
    status = RUN_MEM_LIMIT_ERR;
    goto cleanup;
  }

  if (tst && tst->enable_memory_limit_error && srgp->detect_violations
      && srgp->secure_run && task_IsSecurityViolation(tsk)) {
    status = RUN_SECURITY_ERR;
    goto cleanup;
  }

  // terminated with a signal
  if (task_Status(tsk) == TSK_SIGNALED) {
    cur_info->code = 256; /* FIXME: magic */
    cur_info->termsig = task_TermSignal(tsk);
    status = RUN_RUN_TIME_ERR;
    goto cleanup;
  }

  if (error_code[0]) {
    cur_info->code = error_code_value;
  } else {
    cur_info->code = task_ExitCode(tsk);
  }

  if (srpp->use_info && tstinfo.exit_code > 0) {
    if (cur_info->code != tstinfo.exit_code) {
      status = RUN_WRONG_ANSWER_ERR;
      goto cleanup;
    }
  } else if (srpp->ignore_exit_code > 0) {
    // do not analyze exit code
  } else if (cur_info->code != 0) {
    status = RUN_RUN_TIME_ERR;
    goto cleanup;
  }

  task_Delete(tsk); tsk = NULL;

  if (tsk_int) {
    int exitcode = task_ExitCode(tsk_int);
    if (!exitcode) {
    } else if (exitcode == RUN_PRESENTATION_ERR || exitcode == RUN_WRONG_ANSWER_ERR) {
      status = exitcode;
      goto cleanup;
    } else {
      abort();
    }

    task_Delete(tsk_int); tsk_int = NULL;
  }

run_checker:;

  if (disable_stderr > 0 && cur_info->error_size > 0) {
    append_msg_to_log(check_out_path, "non-empty output to stderr");
    status = RUN_PRESENTATION_ERR;
    goto read_checker_output;
  }

  file_size = -1;
  if (srpp->use_corr) {
    if (srgp->enable_full_archive) {
      filehash_get(corr_src, cur_info->correct_digest);
      cur_info->has_correct_digest = 1;
    } else {
      if (srpp->binary_input <= 0) {
        file_size = generic_file_size(0, corr_src, 0);
      }
      if (file_size >= 0) {
        cur_info->correct_size = file_size;
        if (srgp->max_file_length > 0 && file_size <= srgp->max_file_length) {
          generic_read_file(&cur_info->correct, 0, 0, 0, 0, corr_src, "");
        }
      }
    }
  }

  output_path_to_check = srpp->output_file;
  if (srpp->interactor_cmd && srpp->interactor_cmd[0]) {
    output_path_to_check = output_path;
  }
  status = invoke_checker(srgp, srpp, cur_test, cur_info,
                          check_cmd, test_src, output_path_to_check,
                          corr_src, info_src, tgzdir_src,
                          working_dir, score_out_path, check_out_path,
                          check_dir, ejudge_prefix_dir_env,
                          test_score_count, test_score_val);

  // read the checker output
read_checker_output:;

  file_size = generic_file_size(0, check_out_path, 0);
  if (file_size >= 0) {
    cur_info->chk_out_size = file_size;
    if (!srgp->enable_full_archive) {
      generic_read_file(&cur_info->chk_out, 0, 0, 0, 0, check_out_path, "");
    }
    if (far) {
      snprintf(arch_entry_name, sizeof(arch_entry_name), "%06d.c", cur_test);
      full_archive_append_file(far, arch_entry_name, 0, check_out_path);
    }
  }

cleanup:;

  cur_info->status = status;
  if (pfd1[0] >= 0) close(pfd1[0]);
  if (pfd1[1] >= 0) close(pfd1[1]);
  if (pfd2[0] >= 0) close(pfd2[0]);
  if (pfd2[1] >= 0) close(pfd2[1]);

  if (check_out_path[0]) unlink(check_out_path);
  if (score_out_path[0]) unlink(score_out_path);

  testinfo_free(&tstinfo);
  task_Delete(tsk_int);
  task_Delete(tsk);
  if (check_dir[0]) {
    clear_directory(check_dir);
  }

  return status;

check_failed:
  status = RUN_CHECK_FAILED;
  goto read_checker_output;
}

static void
init_testinfo_vector(struct testinfo_vector *tv)
{
  if (!tv) return;

  memset(tv, 0, sizeof(*tv));
  tv->reserved = 16;
  XCALLOC(tv->data, tv->reserved);
  tv->size = 1;
}

static void
free_testinfo_vector(struct testinfo_vector *tv)
{
  if (tv == NULL || tv->size <= 0 || tv->data == NULL) return;

  for (int i = 0; i < tv->size; ++i) {
    struct testinfo *ti = &tv->data[i];
    xfree(ti->input);
    xfree(ti->output);
    xfree(ti->error);
    xfree(ti->correct);
    xfree(ti->chk_out);
    xfree(ti->args);
    xfree(ti->comment);
    xfree(ti->team_comment);
    xfree(ti->exit_comment);
  }
  memset(tv->data, 0, sizeof(tv->data[0]) * tv->size);
  xfree(tv->data);
  memset(tv, 0, sizeof(*tv));
}

static int
invoke_prepare_cmd(
        const unsigned char *prepare_cmd,
        const unsigned char *working_dir,
        const unsigned char *exe_name,
        const unsigned char *messages_path)
{
  tpTask tsk = task_New();
  int retval = -1;

  task_AddArg(tsk, prepare_cmd);
  task_SetPathAsArg0(tsk);

  task_AddArg(tsk, exe_name);
  task_SetWorkingDir(tsk, working_dir);
  task_SetRedir(tsk, 0, TSR_FILE, "/dev/null", TSK_READ, 0);
  task_SetRedir(tsk, 1, TSR_FILE, messages_path, TSK_REWRITE, TSK_FULL_RW);
  task_SetRedir(tsk, 2, TSR_DUP, 1);
  task_EnableAllSignals(tsk);

  if (task_Start(tsk) < 0) {
    append_msg_to_log(messages_path, "failed to start prepare_cmd %s", prepare_cmd);
    goto cleanup;
  }

  task_Wait(tsk);
  task_Log(tsk, 0, LOG_INFO);
  if (task_IsAbnormal(tsk)) {
    append_msg_to_log(messages_path, "prepare_cmd %s failed", prepare_cmd);
    goto cleanup;
  }

  retval = 0;

cleanup:
  task_Delete(tsk);
  return retval;
}

static int
handle_test_sets(
        const unsigned char *messages_path,
        struct testinfo_vector *tv,
        int score,
        int test_sets_count,
        struct testset_info *test_sets_val)
{
  int ts, i;
  FILE *msgf = NULL;

  if (test_sets_count <= 0) return score;

  for (ts = 0; ts < test_sets_count; ++ts) {
    struct testset_info *ti = &test_sets_val[ts];

    for (i = 1; i < tv->size; ++i) {
      if (tv->data[i].status == RUN_OK
          && (i > ti->total || !ti->nums[i - 1]))
        break;
    }
    if (i < tv->size) continue;
    for (i = 0; i < ti->total; ++i) {
      if (ti->nums[i]
          && (i >= tv->size - 1 || tv->data[i + 1].status != RUN_OK))
        break;
    }
    if (i < ti->total) continue;

    // set the score
    score = ti->score;
    msgf = fopen(messages_path, "a");
    if (msgf) {
      const unsigned char *sep = "";
      fprintf(msgf, "Test set {");
      for (i = 0; i < ti->total; ++i) {
        if (ti->nums[i]) {
          fprintf(msgf, "%s%d", sep, i + 1);
          sep = ", ";
        }
      }
      fprintf(msgf, " } is scored as %d\n", ti->score);
      fclose(msgf); msgf = NULL;
    }
  }
  return score;
}

static void
play_sound(
        const struct section_global_data *global,
        const unsigned char *messages_path,
        int disable_sound,
        int status,
        int passed_tests,
        int score,
        const unsigned char *user_spelling,
        const unsigned char *problem_spelling)
{
  unsigned char b1[64], b2[64], b3[64];
  tpTask tsk = NULL;

  if (!global->sound_player || !global->sound_player[0] || disable_sound > 0) return;

  if (global->extended_sound) {
    tsk = task_New();
    task_AddArg(tsk, global->sound_player);
    snprintf(b1, sizeof(b1), "%d", status);
    snprintf(b2, sizeof(b2), "%d", passed_tests);
    snprintf(b3, sizeof(b3), "%d", score);
    task_AddArg(tsk, b1);
    task_AddArg(tsk, b2);
    task_AddArg(tsk, user_spelling);
    task_AddArg(tsk, problem_spelling);
    task_AddArg(tsk, b3);
  } else {
    const unsigned char *sound = NULL;
    switch (status) {
    case RUN_TIME_LIMIT_ERR:   sound = global->timelimit_sound;    break;
    case RUN_RUN_TIME_ERR:     sound = global->runtime_sound;      break;
    case RUN_CHECK_FAILED:     sound = global->internal_sound;     break;
    case RUN_PRESENTATION_ERR: sound = global->presentation_sound; break;
    case RUN_WRONG_ANSWER_ERR: sound = global->wrong_sound;        break;
    case RUN_OK:               sound = global->accept_sound;       break;
    }
    if (sound) {
      tsk = task_New();
      task_AddArg(tsk, global->sound_player);
      task_AddArg(tsk, sound);
    }
  }

  if (!tsk) return;

  task_SetPathAsArg0(tsk);
  if (task_Start(tsk) < 0) {
    append_msg_to_log(messages_path, "failed to start sound player %s", global->sound_player);
    goto cleanup;
  }

  task_Wait(tsk);

cleanup:
  task_Delete(tsk);
  return;
}

static int
check_output_only(
        const struct section_global_data *global,
        const struct super_run_in_global_packet *srgp,
        const struct super_run_in_problem_packet *srpp,
        full_archive_t far,
        const unsigned char *exe_name,
        struct testinfo_vector *tests,
        const unsigned char *check_cmd,
        const unsigned char *ejudge_prefix_dir_env)
{
  int cur_test = 1;
  struct testinfo *cur_info = NULL;
  int status = RUN_CHECK_FAILED;
  long long file_size = 0;

  unsigned char output_path[PATH_MAX];
  unsigned char score_out_path[PATH_MAX];
  unsigned char check_out_path[PATH_MAX];
  unsigned char arch_entry_name[PATH_MAX];

  unsigned char test_base[PATH_MAX];
  unsigned char corr_base[PATH_MAX];
  unsigned char test_src[PATH_MAX];
  unsigned char corr_src[PATH_MAX];

  // check_cmd, check_dir, global->run_work_dir
  ASSERT(cur_test == tests->size);

  if (tests->size >= tests->reserved) {
    tests->reserved *= 2;
    if (!tests->reserved) tests->reserved = 32;
    tests->data = (typeof(tests->data)) xrealloc(tests->data, tests->reserved * sizeof(tests->data[0]));
  }
  memset(&tests->data[cur_test], 0, sizeof(tests->data[0]));
  cur_info = &tests->data[cur_test];
  ++tests->size;

  test_base[0] = 0;
  test_src[0] = 0;
  if (srpp->test_pat && srpp->test_pat[0]) {
    snprintf(test_base, sizeof(test_base), srpp->test_pat, cur_test);
    snprintf(test_src, sizeof(test_src), "%s/%s", srpp->test_dir, test_base);
  }
  corr_base[0] = 0;
  corr_src[0] = 0;
  if (srpp->corr_pat && srpp->corr_pat[0]) {
    snprintf(corr_base, sizeof(corr_base), srpp->corr_pat, cur_test);
    snprintf(corr_src, sizeof(corr_src), "%s/%s", srpp->corr_dir, corr_base);
  }

  snprintf(check_out_path, sizeof(check_out_path), "%s/checkout_%d.txt",
           global->run_work_dir, cur_test);
  snprintf(score_out_path, sizeof(score_out_path), "%s/scoreout_%d.txt",
           global->run_work_dir, cur_test);

  cur_info->input_size = -1;
  cur_info->output_size = -1;
  cur_info->error_size = -1;
  cur_info->correct_size = -1;
  cur_info->chk_out_size = -1;
  cur_info->visibility = TV_NORMAL;

  snprintf(output_path, sizeof(output_path), "%s/%s", global->run_work_dir, exe_name);
  status = invoke_checker(srgp, srpp, cur_test, cur_info,
                          check_cmd, test_src, output_path,
                          corr_src, NULL, NULL,
                          global->run_work_dir, score_out_path, check_out_path,
                          global->run_work_dir, ejudge_prefix_dir_env,
                          0, NULL);

  if (status == RUN_PRESENTATION_ERR || status == RUN_WRONG_ANSWER_ERR) {
    status = RUN_PARTIAL;
  }

  cur_info->status = status;

  // output file
  file_size = -1;
  if (srpp->binary_input <= 0) {
    file_size = generic_file_size(0, output_path, 0);
  }
  if (file_size >= 0) {
    cur_info->output_size = file_size;
    if (srgp->max_file_length > 0 && !srgp->enable_full_archive && file_size <= srgp->max_file_length) {
      generic_read_file(&cur_info->output, 0, 0, 0, 0, output_path, "");
    }
    if (far) {
      snprintf(arch_entry_name, sizeof(arch_entry_name), "%06d.o", cur_test);
      full_archive_append_file(far, arch_entry_name, 0, output_path);
    }
  }

  file_size = -1;
  if (srpp->use_corr) {
    if (srgp->enable_full_archive) {
      filehash_get(corr_src, cur_info->correct_digest);
      cur_info->has_correct_digest = 1;
    } else {
      if (srpp->binary_input <= 0) {
        file_size = generic_file_size(0, corr_src, 0);
      }
      if (file_size >= 0) {
        cur_info->correct_size = file_size;
        if (srgp->max_file_length > 0 && file_size <= srgp->max_file_length) {
          generic_read_file(&cur_info->correct, 0, 0, 0, 0, corr_src, "");
        }
      }
    }
  }

  file_size = generic_file_size(0, check_out_path, 0);
  if (file_size >= 0) {
    cur_info->chk_out_size = file_size;
    if (!srgp->enable_full_archive) {
      generic_read_file(&cur_info->chk_out, 0, 0, 0, 0, check_out_path, "");
    }
    if (far) {
      snprintf(arch_entry_name, sizeof(arch_entry_name), "%06d.c", cur_test);
      full_archive_append_file(far, arch_entry_name, 0, check_out_path);
    }
  }

  return status;
}

void
run_tests(
        const struct ejudge_cfg *config,
        serve_state_t state,
        const struct section_tester_data *tst,
        const struct super_run_in_packet *srp,
        struct run_reply_packet *reply_pkt,
        int accept_testing,
        int accept_partial,
        int cur_variant,
        char const *exe_name,
        char const *new_base,
        char *report_path,                /* path to the report */
        char *full_report_path,           /* path to the full output dir */
        const unsigned char *user_spelling,
        const unsigned char *problem_spelling,
        int utf8_mode)
{
  const struct section_global_data *global = state->global;
  const struct super_run_in_global_packet *srgp = srp->global;
  const struct super_run_in_problem_packet *srpp = srp->problem;

  full_archive_t far = NULL;

  struct testinfo_vector tests;
  int cur_test = 0;
  int has_real_time = 0;
  int has_max_memory_used = 0;
  int status = RUN_CHECK_FAILED;
  int failed_test;
  int total_score = 0;
  int total_max_score = 0;
  int failed_test_count = 0;
  int has_user_score = 0;
  int user_status = -1;
  int user_score = -1;
  int user_tests_passed = -1;
  int user_run_tests = -1;
  int marked_flag = 0;

  unsigned char ejudge_prefix_dir_env[PATH_MAX];
  unsigned char messages_path[PATH_MAX];
  unsigned char check_dir[PATH_MAX];
  unsigned char check_cmd[PATH_MAX];

  int *open_tests_val = NULL;
  int open_tests_count = 0;

  int *test_score_val = NULL;
  int test_score_count = 0;

  int *score_tests_val = NULL;

  struct testset_info *test_sets_val = NULL;
  int test_sets_count = 0;

  long long expected_free_space = 0;

  char *valuer_errors = NULL;
  char *valuer_comment = NULL;
  char *valuer_judge_comment = NULL;
  char *additional_comment = NULL;

  long report_time_limit_ms = -1;
  long report_real_time_limit_ms = -1;

  init_testinfo_vector(&tests);
  messages_path[0] = 0;

  snprintf(messages_path, sizeof(messages_path), "%s/%s", global->run_work_dir, "messages");

  report_path[0] = 0;
  pathmake(report_path, global->run_work_dir, "/", "report", NULL);
  full_report_path[0] = 0;
  if (srgp->enable_full_archive) {
    pathmake(full_report_path, global->run_work_dir, "/", "full_output", NULL);
    far = full_archive_open_write(full_report_path);
  }

  if (tst && tst->check_dir && tst->check_dir[0]) {
    snprintf(check_dir, sizeof(check_dir), "%s", tst->check_dir);
  } else {
    snprintf(check_dir, sizeof(check_dir), "%s", global->run_check_dir);
  }

  if (srpp->standard_checker && srpp->standard_checker[0]) {
    snprintf(check_cmd, sizeof(check_cmd), "%s/%s",
             global->ejudge_checkers_dir, srpp->standard_checker);
  } else {
    snprintf(check_cmd, sizeof(check_cmd), "%s", srpp->check_cmd);
  }

  if (srpp->type_val) {
    status = check_output_only(global, srgp, srpp, far, exe_name, &tests, check_cmd,
                               ejudge_prefix_dir_env);
    goto done;
  }

  if (srpp->open_tests && srpp->open_tests[0]) {
    if (prepare_parse_open_tests(stderr, srpp->open_tests, &open_tests_val, &open_tests_count) < 0) {
      append_msg_to_log(messages_path, "failed to parse open_tests = '%s'", srpp->open_tests);
      goto check_failed;
    }
  }

  if (srpp->test_score_list && srpp->test_score_list[0]) {
    if (prepare_parse_test_score_list(stderr, srpp->test_score_list, &test_score_val, &test_score_count) < 0) {
      append_msg_to_log(messages_path, "failed to parse test_score_list = '%s'", srpp->test_score_list);
      goto check_failed;
    }
  }

  if (srpp->test_sets && srpp->test_sets[0]) {
    if (prepare_parse_testsets(srpp->test_sets, &test_sets_count, &test_sets_val) < 0) {
      append_msg_to_log(messages_path, "failed to parse test_sets");
      goto check_failed;
    }
  }

  if (srpp->score_tests && srpp->score_tests[0]) {
    if (!(score_tests_val = prepare_parse_score_tests(srpp->score_tests, srpp->full_score))) {
      append_msg_to_log(messages_path, "failed to parse score_tests = '%s'", srpp->score_tests);
      goto check_failed;
    }
  }

#ifdef EJUDGE_PREFIX_DIR
  snprintf(ejudge_prefix_dir_env, sizeof(ejudge_prefix_dir_env),
           "EJUDGE_PREFIX_DIR=%s", EJUDGE_PREFIX_DIR);
#endif /* EJUDGE_PREFIX_DIR */

  if (!srpp->type_val && tst && tst->prepare_cmd && tst->prepare_cmd[0]) {
    if (invoke_prepare_cmd(tst->prepare_cmd, global->run_work_dir, exe_name, messages_path) < 0) {
      goto check_failed;
    }
  }

  /* calculate the expected free space in check_dir */
  expected_free_space = get_expected_free_space(check_dir);

  while (1) {
    ++cur_test;
    if (srgp->scoring_system_val == SCORE_OLYMPIAD
        && accept_testing
        && cur_test > srpp->tests_to_accept) break;

    status = run_one_test(config, state, srp, tst, cur_test, &tests,
                          far, exe_name, report_path, check_cmd,
                          open_tests_count, open_tests_val,
                          test_score_count, test_score_val,
                          expected_free_space,
                          &has_real_time, &has_max_memory_used,
                          &report_time_limit_ms, &report_real_time_limit_ms);
    if (status < 0) {
      status = RUN_OK;
      break;
    }
    if (status > 0) {
      if (srgp->scoring_system_val == SCORE_ACM) break;
      if (srgp->scoring_system_val == SCORE_MOSCOW) break;
      if (srgp->scoring_system_val == SCORE_OLYMPIAD
          && accept_testing && !accept_partial) break;
    }
  }

  /* TESTING COMPLETED */
  get_current_time(&reply_pkt->ts6, &reply_pkt->ts6_us);

  // check failed?
  for (cur_test = 1; cur_test < tests.size; ++cur_test) {
    if (tests.data[cur_test].status == RUN_CHECK_FAILED) break;
  }
  if (cur_test < tests.size) {
    goto check_failed;
  }

  if (srgp->scoring_system_val == SCORE_OLYMPIAD && accept_testing) {
    status = RUN_ACCEPTED;
    failed_test = 0;
    for (cur_test = 1; cur_test <= srpp->tests_to_accept; ++cur_test) {
      if (tests.data[cur_test].status != RUN_OK) {
        status = tests.data[cur_test].status;
        failed_test = cur_test;
        break;
      }
    }
    if (accept_partial) {
      status = RUN_ACCEPTED;
    } else if (srpp->min_tests_to_accept >= 0 && failed_test > srpp->min_tests_to_accept) {
      status = RUN_ACCEPTED;
    }

    reply_pkt->failed_test = failed_test;
    reply_pkt->score = -1;
  } else if (srgp->scoring_system_val == SCORE_KIROV || srgp->scoring_system_val == SCORE_OLYMPIAD) {
    status = RUN_OK;
    total_score = 0;
    failed_test_count = 0;
    for (cur_test = 1; cur_test < tests.size; ++cur_test) {
      if (tests.data[cur_test].status != RUN_OK) {
        status = RUN_PARTIAL;
        ++failed_test_count;
      }
      int this_score = -1;
      if (cur_test < test_score_count) {
        this_score = test_score_val[cur_test];
      }
      if (this_score < 0) {
        this_score = srpp->test_score;
      }
      if (this_score < 0) {
        this_score = 0;
      }
      tests.data[cur_test].score = 0;
      tests.data[cur_test].max_score = this_score;
      total_max_score += this_score;
      
      if (srpp->scoring_checker) {
        total_score += tests.data[cur_test].checker_score;
        tests.data[cur_test].score = tests.data[cur_test].checker_score;
      } else if (tests.data[cur_test].status == RUN_OK) {
        total_score += this_score;
        tests.data[cur_test].score = this_score;
      }
    }

    if (total_max_score > srpp->full_score && (!srpp->valuer_cmd || !srpp->valuer_cmd[0])) {
      append_msg_to_log(messages_path, "Max total score (%d) is greater than full_score",
                        total_max_score, srpp->full_score);
      goto check_failed;
    }

    if (status == RUN_PARTIAL && test_sets_count > 0) {
      total_score = handle_test_sets(messages_path, &tests, total_score,
                                     test_sets_count, test_sets_val);
    }

    if (srpp->variable_full_score <= 0) {
      if (status == RUN_OK) {
        total_score = srpp->full_score;
      } else if (total_score > srpp->full_score) {
        total_score = srpp->full_score;
      }
    } else {
      if (total_score > srpp->full_score) {
        total_score = srpp->full_score;
      }
    }

    // ATTENTION: number of passed test returned is greater than actual by 1,
    // and it is returned in the `failed_test' field
    reply_pkt->failed_test = tests.size - failed_test_count;
    reply_pkt->score = total_score;

    play_sound(global, messages_path, srgp->disable_sound, status,
               tests.size - failed_test_count, total_score, 
               user_spelling, problem_spelling);
  } else {
    reply_pkt->failed_test = tests.size - 1;
    reply_pkt->score = -1;
    if (srgp->scoring_system_val == SCORE_MOSCOW) {
      reply_pkt->score = srpp->full_score;
      if (status != RUN_OK) {
        if (srpp->scoring_checker) {
          reply_pkt->score = tests.data[tests.size - 1].checker_score;
        } else {
          int s;
          for (s = 0; score_tests_val[s] && tests.size - 1 > score_tests_val[s]; ++s);
          reply_pkt->score = s;
        }
      }
    }
  }

  if (srpp->valuer_cmd && srpp->valuer_cmd[0] && !srgp->accepting_mode
      && !reply_pkt->status != RUN_CHECK_FAILED) {
    if (invoke_valuer(global, srp, tests.size, tests.data,
                      cur_variant, srpp->full_score,
                      &total_score, &marked_flag,
                      &user_status, &user_score, &user_tests_passed,
                      &valuer_errors, &valuer_comment,
                      &valuer_judge_comment) < 0) {
      goto check_failed;
    } else {
      reply_pkt->score = total_score;
      reply_pkt->marked_flag = marked_flag;
    }
  }

  if (srgp->separate_user_score <= 0) {
    user_status = -1;
    user_score = -1;
    user_tests_passed = -1;
    user_run_tests = -1;
  } else {
    has_user_score = 1;
    user_run_tests = 0;
    for (cur_test = 1; cur_test < tests.size; ++cur_test) {
      if (tests.data[cur_test].visibility != TV_HIDDEN)
        ++user_run_tests;
    }
    if (user_status < 0) {
      user_status = RUN_OK;
      for (cur_test = 1; cur_test < tests.size; ++cur_test) {
        if (tests.data[cur_test].visibility != TV_HIDDEN
            && tests.data[cur_test].status != RUN_OK) {
          user_status = RUN_PARTIAL;
          break;
        }
      }
    }
    if (srgp->scoring_system_val == SCORE_KIROV
        || (srgp->scoring_system_val == SCORE_OLYMPIAD && !srgp->accepting_mode)) {
      if (user_score < 0) {
        if (srpp->variable_full_score <= 0 && user_status == RUN_OK) {
          if (srpp->full_user_score >= 0) {
            user_score = srpp->full_user_score;
          } else {
            user_score = srpp->full_score;
          }
        } else {
          user_score = 0;
          for (cur_test = 1; cur_test < tests.size; ++cur_test) {
            if (tests.data[cur_test].visibility != TV_HIDDEN
                && tests.data[cur_test].score >= 0) {
              user_score += tests.data[cur_test].score;
            }
          }
          if (srpp->variable_full_score <= 0) {
            if (srpp->full_user_score >= 0 && user_score > srpp->full_user_score) {
              user_score = srpp->full_user_score;
            } else if (user_score > srpp->full_score) {
              user_score = srpp->full_score;
            }
          }
        }
      }
      if (user_tests_passed < 0) {
        user_tests_passed = 0;
        for (cur_test = 1; cur_test < tests.size; ++cur_test) {
          if (tests.data[cur_test].visibility != TV_HIDDEN
              && tests.data[cur_test].status == RUN_OK)
            ++user_tests_passed;
        }
      }
    }
  }

done:;

  long long file_size = -1;
  if (messages_path[0]) {
    file_size = generic_file_size(0, messages_path, 0);
  }
  if (file_size > 0) {
    generic_read_file(&additional_comment, 0, 0, 0, 0, messages_path, "");
  }

  reply_pkt->status = status;
  reply_pkt->has_user_score = has_user_score;
  reply_pkt->user_status = user_status;
  reply_pkt->user_score = user_score;
  reply_pkt->user_tests_passed = user_tests_passed;

  generate_xml_report(srp, reply_pkt, report_path,
                      tests.size, tests.data, utf8_mode,
                      cur_variant, total_score, srpp->full_score, srpp->full_user_score,
                      srpp->use_corr, srpp->use_info,
                      report_time_limit_ms, report_real_time_limit_ms,
                      has_real_time, has_max_memory_used, marked_flag,
                      user_run_tests,
                      additional_comment, valuer_comment,
                      valuer_judge_comment, valuer_errors);

  get_current_time(&reply_pkt->ts7, &reply_pkt->ts7_us);

  if (far) full_archive_close(far);
  free_testinfo_vector(&tests);
  xfree(open_tests_val);
  xfree(test_score_val);
  prepare_free_testsets(test_sets_count, test_sets_val);
  xfree(score_tests_val);
  xfree(valuer_errors);
  xfree(valuer_comment);
  xfree(valuer_judge_comment);
  xfree(additional_comment);
  return;

check_failed:
  if (reply_pkt->ts6 <= 0) {
    get_current_time(&reply_pkt->ts6, &reply_pkt->ts6_us);
  }

  status = RUN_CHECK_FAILED;
  has_user_score = 0;
  user_status = -1;
  user_score = -1;
  user_tests_passed = -1;
  user_run_tests = -1;
  goto done;
}
