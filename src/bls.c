#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "../include/bls.h"

typedef struct buf_t
{
  char   *data;
  size_t  size;
} buf_t;

static size_t callback(char *p, size_t size, size_t nmemb, void *user)
{
  size_t nsize = size * nmemb;
  buf_t *buf = user;
  buf->data = realloc(buf->data, buf->size + nsize + 1);
  if (!buf->data) {
    /* log_error(logger, "%s:%d: realloc(%zu): %s\n", __FILE__, __LINE__, nsize, strerror(errno)); */
    return 0;
  }
  memcpy(&buf->data[buf->size], p, nsize);
  buf->size += nsize;
  buf->data[buf->size] = 0;
  return nsize;
}

static void bls_parse(const buf_t *buf, const char *series, bls_data_handler c, void *u)
{
  char *data = buf->data;
  while (data && *data++ != 0x0A); /* Skip header */

  for (char *tok = data, *savetok = NULL; ; tok = NULL) {
    if (!(tok = strtok_r(tok, BLS_RECORD_DELIM, &savetok))) {
      break;
    }

    struct BLSData d;
    int col = 0;
    for (char *sub = tok, *savesub = NULL; ; sub = NULL, col++) {
      if (!(sub = strtok_r(sub, BLS_FIELD_DELIM, &savesub))) {
        if (col >= 4) {
          c(u, &d);
        }
      BREAK_SUB:
        break;
      }
      switch (col) {
      case 0:
        if (strncmp(sub, series, BLS_SERIES_ID_LENGTH) != 0) {
          goto BREAK_SUB;
        }
        strncpy(d.series, sub, BLS_SERIES_ID_LENGTH);
        break;
      case 1:
        d.year = atoi(sub);
        break;
      case 2:
        strncpy(d.period, sub, BLS_PERIOD_LENGTH);

        if (*d.period == BLS_PERIOD_MONTHLY && strncmp(d.period, BLS_PERIOD_AN_AV, BLS_PERIOD_LENGTH) < 0) {
          sprintf(d.date, "%04d-%2.2s-01", d.year, d.period + 1);
        } else {
          strncpy(d.date, BLS_DATE_NULL, BLS_DATE_LENGTH + 1);
        }
        break;
      case 3:
        d.value = atof(sub);
        break;
      }
    }
  }
}

static void bls_query(const char *url, const char *series, bls_data_handler c, void *u)
{
  buf_t buf = { .data = NULL, .size = 0 };

  curl_global_init(CURL_GLOBAL_ALL);

  CURL *easy = curl_easy_init();
  curl_easy_setopt(easy, CURLOPT_URL, url);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, callback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buf);
  curl_easy_perform(easy);
  curl_easy_cleanup(easy);

  curl_global_cleanup();

  bls_parse(&buf, series, c, u);
  free(buf.data);
}

void bls_download(bls_data_handler callback, void *user)
{
  bls_query(BLS_LABSTAT_CPI_U, BLS_SERIES_ID_CPI_U, callback, user);
  bls_query(BLS_LABSTAT_CPI_W, BLS_SERIES_ID_CPI_W, callback, user);
  bls_query(BLS_LABSTAT_PPI  , BLS_SERIES_ID_PPI  , callback, user);
}
