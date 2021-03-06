/* -*- c -*- */
#ifndef __TELEGRAM_PBS_H__
#define __TELEGRAM_PBS_H__

/* Copyright (C) 2016 Alexander Chernov <cher@ejudge.ru> */

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

struct _bson;

/* persistent bot state for telegram bot */
struct telegram_pbs
{
    unsigned char *_id; // same as bot_id
    long long update_id;
};

struct telegram_pbs *
telegram_pbs_free(struct telegram_pbs *pbs);
struct telegram_pbs *
telegram_pbs_parse_bson(struct _bson *bson);
struct telegram_pbs *
telegram_pbs_create(const unsigned char *_id);
struct _bson *
telegram_pbs_unparse_bson(const struct telegram_pbs *pbs);

struct mongo_conn;

int
telegram_pbs_save(struct mongo_conn *conn, const struct telegram_pbs *pbs);
struct telegram_pbs *
telegram_pbs_fetch(struct mongo_conn *conn, const unsigned char *bot_id);

#endif

/*
 * Local variables:
 *  c-basic-offset: 4
 * End:
 */
