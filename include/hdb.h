#pragma once
#ifndef HDB_H
#define HDB_H

#include <gmodule.h>
#include <sqlite3.h>

#include "../include/bls.h"
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

#define CREATE_SERIES "CREATE TABLE IF NOT EXISTS BLSSeries ("  \
  " Series TEXT(32),"                                           \
  " Year   INTEGER(2),"                                         \
  " Period TEXT(3),"                                            \
  " Value  REAL,"                                               \
  " Date   TEXT(10),"                                           \
  " PRIMARY KEY(Series, Year, Period)"                          \
  ");"

#define UPSERT_SERIES "INSERT INTO BLSSeries"   \
  " (Series, Year, Period, Value, Date)"        \
  " VALUES (?1, ?2, ?3, ?4, ?5)"                \
  " ON CONFLICT (Series, Year, Period)"         \
  " DO NOTHING"

#define SELECT_SERIES       "SELECT Series, Year, Period, Value, Date FROM BLSSeries"
#define SELECT_SERIES_M     SELECT_SERIES " WHERE Period BETWEEN 'M01' AND 'M12'"
#define ORDER_BY_SERIES     " ORDER BY Series, Year, Period"
#define SELECT_SERIES_CPI_U SELECT_SERIES_M " AND Series = '" BLS_SERIES_ID_CPI_U "'" ORDER_BY_SERIES
#define SELECT_SERIES_CPI_W SELECT_SERIES_M " AND Series = '" BLS_SERIES_ID_CPI_W "'" ORDER_BY_SERIES
#define SELECT_SERIES_PPI   SELECT_SERIES_M " AND Series = '" BLS_SERIES_ID_PPI   "'" ORDER_BY_SERIES

struct hdb_t
{
  char *dbpath;
  sqlite3 *db;
  sqlite3_stmt *upsert_history;
  sqlite3_stmt *upsert_series;
  char *errmsg;
};

int  hdb_init(struct hdb_t *, char *);
int  hdb_open(struct hdb_t *);
void hdb_close(struct hdb_t *);

void hdb_upsert_history(struct hdb_t *, const struct YHistory * const);
void hdb_upsert_histories(struct hdb_t *, const YArray * const);

void hdb_upsert_series(struct hdb_t *, const struct BLSData * const);
void *hdb_download_series(void *);
void hdb_select_series(struct hdb_t *, GPtrArray **);

#endif
