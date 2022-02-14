#pragma once
#ifndef HDB_H
#define HDB_H

#include <sqlite3.h>

#include "../include/yql.h"

#define HDB_OK     0
#define HDB_ERROR -1

#define CREATE_HISTORY "CREATE TABLE IF NOT EXISTS YHistory ("  \
  " Symbol    TEXT(32),"                                        \
  " Timestamp INTEGER(8),"                                      \
  " Date      TEXT(32),"                                        \
  " Open      REAL,"                                            \
  " High      REAL,"                                            \
  " Low       REAL,"                                            \
  " Close     REAL,"                                            \
  " AdjClose  REAL,"                                            \
  " Volume    INTEGER(8),"                                      \
  " PRIMARY KEY(Symbol, Timestamp)"                             \
  ");"

#define UPSERT_HISTORY "INSERT INTO YHistory"                           \
  " (Symbol, Timestamp, Date, Open, High, Low, Close, AdjClose, Volume)" \
  " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)"                        \
  " ON CONFLICT (Symbol, Timestamp)"                                    \
  " DO NOTHING"

struct hdb_t
{
  char *dbpath;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *errmsg;
};

int  hdb_init(struct hdb_t *, char *);
int  hdb_open(struct hdb_t *);
void hdb_close(struct hdb_t *);

void hdb_begin(struct hdb_t *);
void hdb_commit(struct hdb_t *);
void hdb_rollback(struct hdb_t *);
void hdb_upsertHistory(struct hdb_t *, const struct YHistory * const);

#endif
