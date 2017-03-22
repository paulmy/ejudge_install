/* -*- mode: c -*- */

/* Copyright (C) 2005-2017 Alexander Chernov <cher@ejudge.ru> */

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

#define NEED_CORR 1
#define NEED_INFO 0
#define NEED_TGZ  0
#include "checker.h"

#include "l10n_impl.h"

int checker_main(int argc, char **argv)
{
  long double out_ans, corr_ans, eps;
  unsigned char *s, *abs_flag;
  int n, i = 0;
  unsigned char buf[32];

  checker_l10n_prepare();

  if (getenv("EJ_REQUIRE_NL")) {
    checker_require_nl(f_out, 1);
  }

  if (!(s = getenv("EPS")))
    fatal_CF(_("Environment variable EPS is not set"));
  if (sscanf(s, "%Lf%n", &eps, &n) != 1 || s[n])
    fatal_CF(_("Cannot parse EPS value"));
  if (eps <= 0.0)
    fatal_CF("EPS <= 0");
  if (eps >= 1)
    fatal_CF("EPS >= 1");
  abs_flag = getenv("ABSOLUTE");

  while (1) {
    i++;
    snprintf(buf, sizeof(buf), "[%d]", i);
    if (checker_read_corr_long_double(buf, 0, &corr_ans) < 0) break;
    if (checker_read_out_long_double(buf, 0, &out_ans) < 0) {
      fatal_WA(_("Too few numbers in the output"));
    }
    if (!(abs_flag?checker_eq_long_double_abs:checker_eq_long_double)(corr_ans, out_ans, eps))
      fatal_WA(_("Answers differ: %s: output: %.15Lg, correct: %.15Lg"), buf, out_ans, corr_ans);
  }
  if (checker_read_out_long_double("x", 0, &out_ans) >= 0) {
    fatal_WA(_("Too many numbers in the output"));
  }
  checker_out_eof();

  checker_OK();
}
