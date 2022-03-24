#include <errno.h>
#include <string.h>

#include "../include/bls.h"
#include "../include/log.h"
#include "../include/plt.h"
#include "../include/stats.h"
#include "../include/util.h"

#define PLT_COMMAND      "gnuplot -persist"
#define PLT_EOD          "e\n"
#define PLT_CHART_PLOT   "./gp/plt_chart_plot.gp"
#define PLT_CHART_OPTION "./gp/plt_chart_option.gp"
#define PLT_CHART_HIST   "./gp/plt_chart_hist.gp"
#define PLT_CHART_BASKET "./gp/plt_chart_basket.gp"
#define PLT_CHART_CORR   "./gp/plt_chart_corr.gp"
#define PLT_CHART_CPPI   "./gp/plt_chart_cppi.gp"

#define plt_gpsend_line(p, f, ...)              \
  fprintf(p, f __VA_OPT__ (,) __VA_ARGS__)

#define plt_gpsend_nl(p)                        \
  plt_gpsend_line(p, "\n")

#define plt_flush(p)                            \
  fflush(p)

Plot plt_gpopen()
{
  Plot p = popen(PLT_COMMAND, "w");
  if (!p) {
    log_default("popen(%s): %s\n", PLT_COMMAND, strerror(errno));
  }
  return p;
}

void plt_gpclose(Plot p)
{
  if (p) {
    if (pclose(p) == -1) {
      log_default("pclose(): %s\n", strerror(errno));
    }
    p = NULL;
  }
}

__attribute__ ((__unused__))
static void plt_gpsend_file(Plot p, const char *filename)
{
  FILE *file = fopen(filename, "r");
  if (!file) {
    log_default("fopen(%s): %s\n", filename, strerror(errno));
    return;
  }

#define LINE_LENGTH 80
  char line[LINE_LENGTH];
  while (fgets(line, LINE_LENGTH, file)) {
    plt_gpsend_line(p, "%s", line);
  }

  if (fclose(file) == EOF) {
    log_default("fclose(): %s\n", strerror(errno));
  }
}

static void plt_gpsend_data(Plot p, const char *data, size_t size __attribute__ ((__unused__)))
{
  plt_gpsend_line(p, "$dat << EOD\n");
  plt_gpsend_line(p, "%s\n", data);
  plt_gpsend_line(p, "EOD\n");
}

static void plt_gpsend_chart(Plot p, const char *var, const struct YChart * const c)
{
  plt_gpsend_line(p, "$%s << EOD\n", var);
  for (size_t i = 0; i < c->count; i++) {
    plt_gpsend_line(p, "%s,%.6f,%.6f,%.6f,%.6f,%.6f,%ld\n", strisodate(c->timestamp[i]),
                    c->open[i], c->high[i], c->low[i], c->close[i], c->adjclose[i], c->volume[i]);
  }
  plt_gpsend_line(p, "EOD\n");
}

static void plt_gpsend_charts(Plot p, const struct YChart * const c[], size_t n)
{
  plt_gpsend_line(p, "array S[%zu]\n", n);
  for (size_t i = 0; i < n; i++) {
    plt_gpsend_line(p, "S[%zu] = '%s'\n", i + 1, c[i]->symbol);
    YString var; snprintf(var, YSTRING_LENGTH, "dat%zu", i + 1);
    plt_gpsend_chart(p, var, c[i]);
  }
}

static void plt_gpsend_bol(Plot p, const struct Bollinger * const b)
{
  plt_gpsend_line(p, "$bol << EOD\n");
  for (size_t i = 0; i < b->size; i++) {
    plt_gpsend_line(p, "%s,%.6f,%.6f\n", strisodate(b->ts[i]), b->ma[i], b->sd[i] * b->K);
  }
  plt_gpsend_line(p, "EOD\n");
}

static void plt_gpsend_history(Plot p, const char *var, const YArray * const A)
{
  plt_gpsend_line(p, "$%s << EOD\n", var);
  for (size_t i = 0; i < A->length; i++) {
    const struct YHistory * const h = YArray_at(A, struct YHistory, i);
    plt_gpsend_line(p, "%s,%.6f,%.6f,%.6f,%.6f,%.6f,%ld\n",
                    h->date, h->open, h->high, h->low, h->close, h->adjclose, h->volume);
  }
  plt_gpsend_line(p, "EOD\n");
}

static void plt_gpsend_table(Plot p, const datable_t * const t)
{
  plt_gpsend_line(p, "$tab << EOD\n");
  for (size_t j = 0; j < t->size; j++) {
    plt_gpsend_line(p, "%s", strisodate(t->timestamp[j]));
    for (size_t i = 0; i < t->rank; i++) {
      plt_gpsend_line(p, ",%.6f", gsl_matrix_get(t->data, i, j));
    }
    plt_gpsend_nl(p);
  }
  plt_gpsend_line(p, "EOD\n");
}

static void plt_gpsend_rcoef(Plot p, const struct SummaryStatistics * const B, size_t n)
{
  plt_gpsend_line(p, "array Br[%zu]\n", n);
  plt_gpsend_line(p, "array B0[%zu]\n", n);
  plt_gpsend_line(p, "array B1[%zu]\n", n);
  for (size_t i = 0; i < n; i++) {
    plt_gpsend_line(p, "Br[%zu] = %.6f\n", i + 1, B[i].correlation);
    plt_gpsend_line(p, "B0[%zu] = %.6f\n", i + 1, B[i].intercept);
    plt_gpsend_line(p, "B1[%zu] = %.6f\n", i + 1, B[i].slope);
  }
}

static void plt_gpsend_heatmap(Plot p, const datable_t * const t)
{
  plt_gpsend_line(p, "$map << EOD\n");
  for (size_t i = 0; i < t->rank; i++) {
    plt_gpsend_line(p, ",%s", t->charts[i]->symbol); /* rowheaders */
  }
  plt_gpsend_nl(p);
  for (size_t i = 0; i < t->rank; i++) {
    plt_gpsend_line(p, "%s", t->charts[i]->symbol);  /* columnheaders */
    for (size_t j = 0; j < t->rank; j++) {
      plt_gpsend_line(p, ",%.6f", gsl_matrix_get(t->rmat, i, j));
    }
    plt_gpsend_nl(p);
  }
  plt_gpsend_line(p, "EOD\n");
}

static void plt_gpsend_cppi(Plot p, const GPtrArray * const g)
{
  plt_gpsend_line(p, "$dat << EOD\n");
  const char *series = ((struct BLSData *) g_ptr_array_index(g, 0))->series;
  for (guint i = 0; i < g->len; i++) {
    const struct BLSData * const d = g_ptr_array_index(g, i);
    if (strncmp(d->series, series, BLS_SERIES_ID_LENGTH) != 0) {
      series = d->series;
      plt_gpsend_line(p, "\n\n");
    }
    plt_gpsend_line(p, "%s,%.3f\n", d->date, d->value);
  }
  plt_gpsend_line(p, "EOD\n");
}

static void plt_chart_bol(Plot p, const struct YChart * const c)
{
  struct Bollinger b = { .N = 10, .K = 1.5, };
  bollinger_c(&b, c);
  plt_gpsend_bol(p, &b);
  Bollinger_free(&b);
}

void plt_gpplot_chart(Plot p, const struct YChart * const c)
{
  if (!p || !c || !c->count) {
    return;
  }

  int64_t startDate = c->timestamp[0];
  int64_t endDate = c->timestamp[c->count - 1];
  const struct YQuote * const q = yql_quote_get(c->symbol);
  const struct YQuoteSummary * const s = yql_quoteSummary_get(c->symbol);
  double targetMeanPrice = s ? s->financialData.targetMeanPrice : 0.0;

  plt_gpsend_chart(p, "dat", c);
  plt_chart_bol(p, c);
  plt_gpsend_line(p, "call '%s' '%s' '%ld' '%ld' '%.6f' '%.6f' '%.6f'\n", PLT_CHART_PLOT,
                  c->symbol, startDate, endDate, q->fiftyDayAverage, q->twoHundredDayAverage, targetMeanPrice);
  plt_flush(p);
}

void plt_gpplot_option(Plot p, const struct YChart * const c, const struct YChart * const d)
{
  if (!p || !c || !c->count) {
    return;
  }

  int64_t startDate = c->timestamp[0];
  int64_t endDate = c->timestamp[c->count - 1];
  double strike = yql_quote_get(c->symbol)->strike;

  plt_gpsend_chart(p, "dat", c);
  plt_gpsend_chart(p, "und", d);
  plt_gpsend_line(p, "call '%s' '%s' '%ld' '%ld' '%s' '%.6f'\n", PLT_CHART_OPTION,
                  c->symbol, startDate, endDate, d->symbol, strike);
  plt_flush(p);
}

void plt_gpplot_histdata(Plot p, const char *s, int64_t startDate, int64_t endDate, const char *data, size_t size)
{
  if (!p || !data || !size) {
    return;
  }

  plt_gpsend_data(p, data, size);
  plt_gpsend_line(p, "call '%s' '%s' '%ld' '%ld'\n", PLT_CHART_HIST, s, startDate, endDate);
  plt_flush(p);
}

void plt_gpplot_history(Plot p, const YArray * const A)
{
  if (!p || !A || !A->length) {
    return;
  }

  const char *s = YArray_at(A, struct YHistory, 0)->symbol;
  int64_t startDate = strpts(YArray_at(A, struct YHistory, 0)->date);
  int64_t endDate = strpts(YArray_at(A, struct YHistory, A->length - 1)->date);

  plt_gpsend_history(p, "dat", A);
  plt_gpsend_line(p, "call '%s' '%s' '%ld' '%ld'\n", PLT_CHART_HIST, s, startDate, endDate);
  plt_flush(p);
}

static void plt_table_fit(Plot p, const struct YChart *C[], size_t n)
{
  datable_t t;
  datable_init(&t, n, C);
  datable_fit(&t);
  if (t.size) {
    plt_gpsend_table(p, &t);
    plt_gpsend_rcoef(p, t.bvec, t.rank);
    plt_gpsend_heatmap(p, &t);
  }
  datable_free(&t);
}

void plt_gpplot_basket(Plot p, const struct YChart * C[], size_t n)
{
  if (!p || !C || !n) {
    return;
  }

  const struct YChart * const c = C[0];
  int64_t startDate = c->timestamp[0];
  int64_t endDate = c->timestamp[c->count - 1];

  plt_gpsend_charts(p, C, n);
  plt_chart_bol(p, c);
  plt_table_fit(p, C, n);
  plt_gpsend_line(p, "call '%s' '%ld' '%ld' '%zu'\n", PLT_CHART_BASKET, startDate, endDate, n);
  plt_flush(p);
}

void plt_gpplot_corr(Plot p, const struct YChart * C[], size_t n)
{
  if (!p || !C || !n) {
    return;
  }

  const struct YChart * const c = C[0];
  int64_t startDate = c->timestamp[0];
  int64_t endDate = c->timestamp[c->count - 1];

  plt_gpsend_charts(p, C, n);
  plt_table_fit(p, C, n);
  plt_gpsend_line(p, "call '%s' '%ld' '%ld' '%ld'\n", PLT_CHART_CORR, startDate, endDate, n);
  plt_flush(p);
}

void plt_gpplot_cppi(Plot p, const GPtrArray * const g)
{
  if (!p || !g || !g->len) {
    return;
  }

  plt_gpsend_cppi(p, g);
  plt_gpsend_line(p, "call '%s'\n", PLT_CHART_CPPI);
  plt_flush(p);
}
