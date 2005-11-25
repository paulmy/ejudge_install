/* -*- c -*- */
/* $Id$ */

/* Copyright (C) 2005 Alexander Chernov <cher@ispras.ru> */

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

#include "prepare.h"
#include "prepare_vars.h"
#include "settings.h"
#include "varsubst.h"
#include "version.h"
#include "prepare_serve.h"
#include "teamdb.h"
#include "errlog.h"

#include <reuse/xalloc.h>

int
find_variant(int user_id, int prob_id)
{
  int i, new_vint;
  struct variant_map *pmap = global->variant_map;

  if (!pmap) return 0;
  if (prob_id <= 0 || prob_id > max_prob || !probs[prob_id]) return 0;
  if (probs[prob_id]->variant_num <= 0) return 0;
  if (!pmap->prob_map[prob_id]) return 0;

  teamdb_refresh();
  new_vint = teamdb_get_vintage();
  if (new_vint != pmap->vintage || !pmap->user_map_size || !pmap->user_map) {
    info("find_variant: new vintage: %d, old: %d, updating variant map",
         new_vint, pmap->vintage);
    xfree(pmap->user_map);
    pmap->user_map_size = 0;
    pmap->user_map = 0;

    pmap->user_map_size = teamdb_get_max_team_id() + 1;
    XCALLOC(pmap->user_map, pmap->user_map_size);

    for (i = 0; i < pmap->u; i++) {
      pmap->v[i].user_id = teamdb_lookup_login(pmap->v[i].login);
      if (pmap->v[i].user_id < 0) pmap->v[i].user_id = 0;
      if (!pmap->v[i].user_id) continue;
      if (pmap->v[i].user_id >= pmap->user_map_size) continue;
      pmap->user_map[pmap->v[i].user_id] = &pmap->v[i];
    }
    pmap->vintage = new_vint;
  }

  if (user_id <= 0 || user_id >= pmap->user_map_size) return 0;
  if (pmap->user_map[user_id])
    return pmap->user_map[user_id]->variants[pmap->prob_map[prob_id]];
  return 0;
}

int
find_user_priority_adjustment(int user_id)
{
  struct user_adjustment_map *pmap = global->user_adjustment_map;
  struct user_adjustment_info *pinfo = global->user_adjustment_info;
  int new_vint, i;

  if (!pinfo) return 0;
  new_vint = teamdb_get_vintage();
  if (!pmap || new_vint != pmap->vintage) {
    if (!pmap) {
      XCALLOC(pmap, 1);
      global->user_adjustment_map = pmap;
    }
    xfree(pmap->user_map);

    pmap->user_map_size = teamdb_get_max_team_id() + 1;
    XCALLOC(pmap->user_map, pmap->user_map_size);

    for (i = 0; pinfo[i].login; i++) {
      pinfo[i].id = teamdb_lookup_login(pinfo[i].login);
      if (pinfo[i].id <= 0 || pinfo[i].id >= pmap->user_map_size) {
        pinfo[i].id = 0;
        continue;
      }
      if (pmap->user_map[pinfo[i].id]) continue;
      pmap->user_map[pinfo[i].id] = &pinfo[i];
    }

    pmap->vintage = new_vint;
  }

  if (user_id <= 0 || user_id >= pmap->user_map_size) return 0;
  if (!pmap->user_map[user_id]) return 0;
  return pmap->user_map[user_id]->adjustment;
}

int
prepare_serve_defaults(void)
{
  int i;

#if defined EJUDGE_CONTESTS_DIR
  if (!global->contests_dir[0]) {
    snprintf(global->contests_dir, sizeof(global->contests_dir),
             "%s", EJUDGE_CONTESTS_DIR);
  }
#endif /* EJUDGE_CONTESTS_DIR */
  if (!global->contests_dir[0]) {
    err("global.contests_dir must be set");
    return -1;
  }
  if ((i = contests_set_directory(global->contests_dir)) < 0) {
    err("invalid contests directory '%s': %s", global->contests_dir,
        contests_strerror(-i));
    return -1;
  }
  if ((i = contests_get(global->contest_id, &cur_contest)) < 0) {
    err("cannot load contest information: %s",
        contests_strerror(-i));
    return -1;
  }
  snprintf(global->name, sizeof(global->name), "%s", cur_contest->name);
  return 0;
}

/**
 * Local variables:
 *  compile-command: "make"
 *  c-font-lock-extra-types: ("\\sw+_t" "FILE")
 * End:
 */
