#include "../include/hdb.h"
#include "../include/log.h"

int hdb_init(struct hdb_t *hdb, char *dbpath)
{
  hdb->dbpath = dbpath;
  hdb->db = NULL;
  hdb->stmt = NULL;
  hdb->errmsg = NULL;
  return HDB_OK;
}

static int hdb_exec(struct hdb_t *hdb, const char *sql)
{
  if (!hdb->db) {
    return HDB_ERROR;
  }

  if (sqlite3_exec(hdb->db, sql, NULL, NULL, &hdb->errmsg)) {
    log_default("sqlite3_exec(%s): %s\n", sql, hdb->errmsg);
    sqlite3_free(hdb->errmsg);
    return HDB_ERROR;
  }
  return HDB_OK;
}

static int hdb_create(struct hdb_t *hdb)
{
  return hdb_exec(hdb, CREATE_HISTORY);
}

int hdb_open(struct hdb_t *hdb)
{
  if (sqlite3_open(hdb->dbpath, &hdb->db) != SQLITE_OK) {
    log_default("sqlite3_open(%s): %s\n", hdb->dbpath, sqlite3_errmsg(hdb->db));
    hdb_close(hdb);
    return HDB_ERROR;
  }
  if (hdb_create(hdb) != HDB_OK) {
    hdb_close(hdb);
    return HDB_ERROR;
  }
  if (sqlite3_prepare_v3(hdb->db, UPSERT_HISTORY, -1, SQLITE_PREPARE_PERSISTENT, &hdb->stmt, NULL) != SQLITE_OK) {
    log_default("sqlite3_prepare_v3(UPSERT_HISTORY): %s\n", sqlite3_errmsg(hdb->db));
    hdb_close(hdb);
    return HDB_ERROR;
  }
  return HDB_OK;
}

void hdb_close(struct hdb_t *hdb)
{
  if (sqlite3_finalize(hdb->stmt) != SQLITE_OK) {
    log_default("sqlite3_finalize(UPSERT_HISTORY): %s\n", sqlite3_errmsg(hdb->db));
  }
  hdb->stmt = NULL;
  if (sqlite3_close(hdb->db) != SQLITE_OK) {
    log_default("sqlite3_close(%s): %s\n", hdb->dbpath, sqlite3_errmsg(hdb->db));
  }
  hdb->db = NULL;
}

void hdb_begin(struct hdb_t *hdb)
{
  hdb_exec(hdb, "BEGIN DEFERRED TRANSACTION");
}

void hdb_commit(struct hdb_t *hdb)
{
  hdb_exec(hdb, "COMMIT");
}

void hdb_rollback(struct hdb_t *hdb)
{
  hdb_exec(hdb, "ROLLBACK");
}

static void hdb_execute(struct hdb_t *hdb)
{
  sqlite3_step(hdb->stmt);
  sqlite3_reset(hdb->stmt);
  sqlite3_clear_bindings(hdb->stmt);
}

void hdb_upsertHistory(struct hdb_t *hdb, const struct YHistory * const h)
{
  if (!hdb->db || !hdb->stmt) {
    return;
  }

  int i = 1;
  sqlite3_bind_text   (hdb->stmt, i++, h->symbol, -1, SQLITE_STATIC);
  sqlite3_bind_int64  (hdb->stmt, i++, h->timestamp);
  sqlite3_bind_text   (hdb->stmt, i++, h->date, -1, SQLITE_STATIC);
  sqlite3_bind_double (hdb->stmt, i++, h->open);
  sqlite3_bind_double (hdb->stmt, i++, h->high);
  sqlite3_bind_double (hdb->stmt, i++, h->low);
  sqlite3_bind_double (hdb->stmt, i++, h->close);
  sqlite3_bind_double (hdb->stmt, i++, h->adjclose);
  sqlite3_bind_int64  (hdb->stmt, i++, h->volume);
  hdb_execute(hdb);
}
