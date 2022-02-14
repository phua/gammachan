#include <errno.h>
#include <string.h>

#include "../include/log.h"
#include "../include/plt.h"
#include "../include/util.h"

#define PLT_COMMAND    "gnuplot -persist"
#define PLT_EOD        "e\n"
#define PLT_CHART_INIT "./gp/plt_chart_init.gp"
#define PLT_CHART_PLOT "./gp/plt_chart_plot.gp"
#define PLT_CHART_HIST "./gp/plt_chart_hist.gp"

#define plt_gpsend_line(p, f, ...)              \
  fprintf(p, f __VA_OPT__ (,) __VA_ARGS__)

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

static void plt_gpsend_chart(Plot p, const struct YChart * const c)
{
  plt_gpsend_line(p, "$dat << EOD\n");
  for (size_t i = 0; i < c->count; i++) {
    plt_gpsend_line(p, "%s,%.6f,%.6f,%.6f,%.6f,%.6f,%ld\n", strydate(c->timestamp[i]),
                    c->open[i], c->high[i], c->low[i], c->close[i], c->adjclose[i], c->volume[i]);
  }
  plt_gpsend_line(p, "EOD\n");
}

void plt_gpplot_chart(Plot p, const struct YChart * const c)
{
  if (!p || !c || !c->count) {
    return;
  }

  const struct YQuote * const q = yql_getQuote(c->symbol);
  const struct YQuoteSummary * const s = yql_getQuoteSummary(c->symbol);
  double targetMeanPrice = s ? s->financialData.targetMeanPrice : 0.0;

  plt_gpsend_line(p, "load '%s'\n", PLT_CHART_INIT);
  plt_gpsend_chart(p, c);
  plt_gpsend_line(p, "call '%s' '%s' '%ld' '%.6f' '%.6f' '%.6f'\n", PLT_CHART_PLOT,
                  c->symbol, c->timestamp[0], q->fiftyDayAverage, q->twoHundredDayAverage, targetMeanPrice);
  plt_flush(p);
}

void plt_gpplot_hist(Plot p, const char *s, int64_t startDate, int64_t endDate, const char *data, size_t size)
{
  if (!p || !data || !size) {
    return;
  }

  plt_gpsend_line(p, "load '%s'\n", PLT_CHART_INIT);
  plt_gpsend_data(p, data, size);
  plt_gpsend_line(p, "call '%s' '%s' '%ld' '%ld'\n", PLT_CHART_HIST, s, startDate, endDate);
  plt_flush(p);
}
