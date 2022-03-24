#include "../include/hdb.h"
#include "../include/log.h"
#include "../include/util.h"

typedef int (*exec_callback)(void *, int, char **, char **);

int hdb_init(struct hdb_t *hdb, char *dbpath)
{
  hdb->dbpath = dbpath;
  hdb->db = NULL;
  hdb->upsert_history = NULL;
  hdb->upsert_series = NULL;
  hdb->errmsg = NULL;
  return HDB_OK;
}

static int exec_query(struct hdb_t *hdb, const char *sql, exec_callback c, void *u)
{
  if (!hdb->db) {
    return HDB_ERROR;
  }

  if (sqlite3_exec(hdb->db, sql, c, u, &hdb->errmsg)) {
    log_default("sqlite3_exec(%s): %s\n", sql, hdb->errmsg);
    sqlite3_free(hdb->errmsg);
    return HDB_ERROR;
  }
  return HDB_OK;
}

static int exec_stmt(struct hdb_t *hdb, const char *sql)
{
  return exec_query(hdb, sql, NULL, NULL);
}

static int hdb_create(struct hdb_t *hdb)
{
  int status = HDB_OK;
  if ((status = exec_stmt(hdb, CREATE_HISTORY)) != HDB_OK) {
    return status;
  }
  if ((status = exec_stmt(hdb, CREATE_SERIES)) != HDB_OK) {
    return status;
  }
  return status;
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
  if (sqlite3_prepare_v3(hdb->db, UPSERT_HISTORY, -1, SQLITE_PREPARE_PERSISTENT, &hdb->upsert_history, NULL) != SQLITE_OK) {
    log_default("sqlite3_prepare_v3(UPSERT_HISTORY): %s\n", sqlite3_errmsg(hdb->db));
    hdb_close(hdb);
    return HDB_ERROR;
  }
  if (sqlite3_prepare_v3(hdb->db, UPSERT_SERIES, -1, SQLITE_PREPARE_PERSISTENT, &hdb->upsert_series, NULL) != SQLITE_OK) {
    log_default("sqlite3_prepare_v3(UPSERT_SERIES): %s\n", sqlite3_errmsg(hdb->db));
    hdb_close(hdb);
    return HDB_ERROR;
  }
  return HDB_OK;
}

void hdb_close(struct hdb_t *hdb)
{
  if (sqlite3_finalize(hdb->upsert_history) != SQLITE_OK) {
    log_default("sqlite3_finalize(UPSERT_HISTORY): %s\n", sqlite3_errmsg(hdb->db));
  }
  hdb->upsert_history = NULL;
  if (sqlite3_finalize(hdb->upsert_series) != SQLITE_OK) {
    log_default("sqlite3_finalize(UPSERT_SERIES): %s\n", sqlite3_errmsg(hdb->db));
  }
  hdb->upsert_series = NULL;
  if (sqlite3_close(hdb->db) != SQLITE_OK) {
    log_default("sqlite3_close(%s): %s\n", hdb->dbpath, sqlite3_errmsg(hdb->db));
  }
  hdb->db = NULL;
}

static void hdb_begin(struct hdb_t *hdb)
{
  exec_stmt(hdb, "BEGIN DEFERRED TRANSACTION");
}

static void hdb_commit(struct hdb_t *hdb)
{
  exec_stmt(hdb, "COMMIT");
}

__attribute__ ((__unused__))
static void hdb_rollback(struct hdb_t *hdb)
{
  exec_stmt(hdb, "ROLLBACK");
}

static void exec_pstmt(sqlite3_stmt *stmt)
{
  sqlite3_step(stmt);
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
}

void hdb_upsert_history(struct hdb_t *hdb, const struct YHistory * const h)
{
  if (!hdb->db || !hdb->upsert_history) {
    return;
  }

  int i = 1;
  sqlite3_bind_text   (hdb->upsert_history, i++, h->symbol, -1, SQLITE_STATIC);
  sqlite3_bind_int64  (hdb->upsert_history, i++, strpts(h->date));
  sqlite3_bind_text   (hdb->upsert_history, i++, h->date, -1, SQLITE_STATIC);
  sqlite3_bind_double (hdb->upsert_history, i++, h->open);
  sqlite3_bind_double (hdb->upsert_history, i++, h->high);
  sqlite3_bind_double (hdb->upsert_history, i++, h->low);
  sqlite3_bind_double (hdb->upsert_history, i++, h->close);
  sqlite3_bind_double (hdb->upsert_history, i++, h->adjclose);
  sqlite3_bind_int64  (hdb->upsert_history, i++, h->volume);
  exec_pstmt(hdb->upsert_history);
}

void hdb_upsert_histories(struct hdb_t *hdb, const YArray * const A)
{
  if (!hdb->db) {
    return;
  }

  hdb_begin(hdb);
  for (size_t i = 0; i < A->length; i++)  {
    hdb_upsert_history(hdb, YArray_at(A, struct YHistory, i));
  }
  hdb_commit(hdb);
}

void hdb_upsert_series(struct hdb_t *hdb, const struct BLSData * const d)
{
  if (!hdb->db || !hdb->upsert_series) {
    return;
  }

  int i = 1;
  sqlite3_bind_text   (hdb->upsert_series, i++, d->series, -1, SQLITE_STATIC);
  sqlite3_bind_int    (hdb->upsert_series, i++, d->year);
  sqlite3_bind_text   (hdb->upsert_series, i++, d->period, -1, SQLITE_STATIC);
  sqlite3_bind_double (hdb->upsert_series, i++, d->value);
  sqlite3_bind_text   (hdb->upsert_series, i++, d->date, -1, SQLITE_STATIC);
  exec_pstmt(hdb->upsert_series);
}

void *hdb_download_series(void *arg)
{
  void c(void *u, const struct BLSData *d) { hdb_upsert_series(u, d); }

  struct hdb_t *hdb = arg;

  if (hdb->db) {
    hdb_begin(hdb);
    bls_download(c, hdb);
    hdb_commit(hdb);
  }

  return NULL;
}

static int select_series_callback(void *u, int argc _U_, char **argv, char **argcn _U_)
{
  struct BLSData *d = malloc(sizeof(struct BLSData));
  strncpy(d->series, argv[0], BLS_SERIES_ID_LENGTH);
  d->year = atoi(argv[1]);
  strncpy(d->period, argv[2], BLS_PERIOD_LENGTH);
  d->value = atof(argv[3]);
  strncpy(d->date, argv[4], BLS_DATE_LENGTH);

  g_ptr_array_add(u, d);
  return 0;
}

void hdb_select_series(struct hdb_t *hdb, GPtrArray **g)
{
  if (!*g) {
    *g = g_ptr_array_new_full(512, free);
  }

  exec_query(hdb, SELECT_SERIES_CPI_U, select_series_callback, *g);
  exec_query(hdb, SELECT_SERIES_CPI_W, select_series_callback, *g);
  exec_query(hdb, SELECT_SERIES_PPI  , select_series_callback, *g);
}
