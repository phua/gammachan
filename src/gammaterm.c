#include <ctype.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "../include/config.h"
#include "../include/gammaterm.h"
#include "../include/hdb.h"
#include "../include/layout.h"
#include "../include/log.h"
#include "../include/plt.h"
#include "../include/pm.h"
#include "../include/util.h"
#include "../include/yql.h"

#define DEBUG(f, ...)                           \
  fprintf(stderr, f, ## __VA_ARGS__)

#define DBGYX(s)                                                        \
  DEBUG("DBGYX %s: maxy=%d, maxx=%d, begy=%d, begx=%d, cury=%d, curx=%d\n", \
        s, maxy, maxx, begy, begx, cury, curx)

#define getallyx(win)                           \
  getmaxyx(win, maxy, maxx);                    \
  getbegyx(win, begy, begx);                    \
  getyx(win, cury, curx)

#define mvwdelchr(win, y, x)                    \
  /* mvwdelch(win, (y), (x)); */                \
  mvwaddch(win, (y), (x), ' ');                 \
  wmove(win, (y), (x))

#define mvwclrtolen(win, y, x, w)               \
  mvwprintw(win, (y), (x), "%*s", (w), "")

#define waddstrcp(win, cp, s)                   \
  wattron(win, cp);                             \
  waddstr(win, s);                              \
  wattroff(win, cp)

#define mvwaddstrcp(win, y, x, cp, s)           \
  wmove(win, (y), (x));                         \
  waddstrcp(win, cp, s)

#define mvwaddstrcn(win, y, x, w, cp, s)                        \
  mvwaddstrcp(win, (y), ((x) + (((w) - strlen(s)) / 2)), cp, s)

#define wprintwcp(win, cp, f, ...)              \
  wattron(win, cp);                             \
  wprintw(win, f __VA_OPT__ (,) __VA_ARGS__);   \
  wattroff(win, cp)

#define mvwprintwcp(win, y, x, cp, f, ...)          \
  wmove(win, (y), (x));                             \
  wprintwcp(win, cp, f __VA_OPT__ (,) __VA_ARGS__)

#define mvwprintkey(win, y, x, w, s, r, c)                              \
  mvwaddstrcp(win, ((y) + (r)), ((x) + ((w) * (c))), COLOR_PAIR_KEY, s)

#define mvwprintval(win, y, x, w, f, r, c, cp, ...)                     \
  mvwprintwcp(win, ((y) + (r)), ((x) + ((w) * (c))), cp, f __VA_OPT__ (,) __VA_ARGS__)

#define mvwprintkeyval(win, y, x, w, s, f, sr, sc, fr, fc, cp, ...)     \
  mvwprintkey(win, (y), (x), (w), s, (sr), (sc));                       \
  mvwprintval(win, (y), (x), (w), f, (fr), (fc), cp __VA_OPT__ (,) __VA_ARGS__)

static int maxy, maxx, begy, begx, cury, curx;

static WINDOW *w_top;
static WINDOW *w_bot;
static WINDOW *w_pop;
static PANEL         *panels[PANEL_TYPES];
static struct Spark  *sparks[PANEL_TYPES];
static enum PanelType curpan = HELP;

static struct EventCalendar calendar;
static struct hdb_t hdb;
static Plot plot = NULL;

static struct iextp_config config;
static GPtrArray *portfolios = NULL;
static guint curpor = 0;

#define setnext(i, n) (i = ((n) ? (((i) + 1) % (n)) : 0))
#define setprev(i, n) (i = ((n) ? (((i) + ((n) - 1)) % (n)) : 0))

#define getcurrpan()  (panels[curpan])
#define setcurrpan(p) (curpan = ((p) % PANEL_TYPES))
#define setnextpan()  (setnext(curpan, PANEL_TYPES))
#define setprevpan()  (setprev(curpan, PANEL_TYPES))
#define getcurrspr()  (sparks[curpan])

#define getcurrpor()  (curpor < portfolios->len ? g_ptr_array_index(portfolios, curpor) : NULL)
#define setcurrpor(p) (curpor = p < portfolios->len ? p : curpor)
#define setnextpor()  (setnext(curpor, portfolios->len))
#define setprevpor()  (setprev(curpor, portfolios->len))

static void wprint_top(WINDOW *win)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstr(win, y, x, TERM_NAME);
  mvwaddstrcn(win, y, x, w, COLOR_PAIR_HOTKEY, TERM_HOTKEYS);
}

static void *start_clock(void *arg)
{
  static time_t tm;
  static char str[32];

  tm = time(NULL);
  ctime_r(&tm, str);

  WINDOW *win = (WINDOW *) arg;
  getallyx(win);

  int y = MARGIN_Y, x = maxx - MARGIN_X - (strnlen(str, 32) - 1);
  mvwprintw(win, y, x, "%.24s", str);

  wnoutrefresh(win);

  return NULL;
}

static void wprint_bot(WINDOW *win, const char *prompt)
{
  getallyx(win);
  wclear(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X;
  mvwaddstrcp(win, y, x, COLOR_PAIR_CMD, prompt);
}

static bool wmgetch(WINDOW *win)
{
  while (1) {
    switch (wgetch(win)) {
    case GTKEY_GO:
      return true;
    case GTKEY_CANCEL:
    case 'Q':
    case 'q':
      return false;
    }
  }
  /* __builtin_unreachable(); */
  return false;
}

static bool wprint_pop(WINDOW *win, const char *resp, const char *code, const char *desc, const char *sym)
{
  getallyx(win);
  wclear(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcn(win, y++, x, w, COLOR_PAIR_ERROR, code);
  mvwhline(win, y++, x, ACS_HLINE, w);
  mvwaddstrcp(win, y, x, COLOR_PAIR_KEY, "symbol: ");
  waddstr(win, sym);
  mvwprintwcp(win, ++y, x, COLOR_PAIR_KEY, "%s.description: ", resp);
  mvwaddstr(win, ++y, x + MARGIN_X, desc);

  y = maxy - 2, w /= 2;
  mvwaddstrcn(win, y, x + w * 0, w, COLOR_PAIR_GO    , TERM_BTN_GO);
  mvwaddstrcn(win, y, x + w * 1, w, COLOR_PAIR_CANCEL, TERM_BTN_CANCEL);

  wrefresh(win);

  return wmgetch(win);
}

static bool wprint_err(WINDOW *win, struct YError *err, const char *sym)
{
  return wprint_pop(win, err->response, err->code, err->description, sym);
}

static void wprint_blank(WINDOW *win)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcn(win, y, x, w, COLOR_PAIR_INFO, "This panel intentionally left blank.");
}

static void wprint_help(WINDOW *win)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2, cp = COLOR_PAIR_DEFAULT;
  cp = COLOR_PAIR_CANCEL;
  mvwaddstrcp(win, y++, x, cp, "ESC      CANCEL          Cancel");
  cp = COLOR_PAIR_GO;
  mvwaddstrcp(win, y++, x, cp, "F1       HELP            Help");
  cp = COLOR_PAIR_HOTKEY;
  mvwaddstrcp(win, y++, x, cp, "F2       GOVT            Government securities");
  mvwaddstrcp(win, y++, x, cp, "F3       CORP            Corporate debt");
  mvwaddstrcp(win, y++, x, cp, "F4       MTGE            Mortgage securities");
  mvwaddstrcp(win, y++, x, cp, "F5       M-MKT           Money market");
  mvwaddstrcp(win, y++, x, cp, "F6       MUNI            Municipal debt");
  mvwaddstrcp(win, y++, x, cp, "F7       PFD             Preferred shares");
  mvwaddstrcp(win, y++, x, cp, "F8       EQUITY          Equity shares");
  mvwaddstrcp(win, y++, x, cp, "F9       CMDTY           Commodity markets");
  mvwaddstrcp(win, y++, x, cp, "F10      INDEX           Indices");
  mvwaddstrcp(win, y++, x, cp, "F11      CRNCY           Currency markets");
  mvwaddstrcp(win, y++, x, cp, "F12      CLIENT/ALPHA    Portfolio functionality");
  cp = COLOR_PAIR_GO;
  mvwaddstrcp(win, y++, x, cp, "ENTER    GO              Go");
  mvwhline(win, y++, x, ACS_HLINE, w);
  cp = COLOR_PAIR_INFO;
  mvwaddstrcp(win, y++, x, cp, "<F(n)>                   Goto (refresh) panel");
  mvwaddstrcp(win, y++, x, cp, "hjkl                     Goto previous / next panel");
  mvwaddstrcp(win, y++, x, cp, "1...9                    Goto portfolio");
  mvwaddstrcp(win, y++, x, cp, "HJKL                     Goto previous / next portfolio");
  cp = COLOR_PAIR_CANCEL;
  mvwaddstrcp(win, y++, x, cp, "Q                        Quit");
  mvwhline(win, y++, x, ACS_HLINE, w);
  cp = COLOR_PAIR_BLUE;
  mvwaddstrcp(win, y++, x, cp, "<ESC>                    Exit view mode");
  mvwaddstrcp(win, y++, x, cp, "0                        View watchlist mode");
  mvwaddstrcp(win, y++, x, cp, "C                        Plot chart");
  mvwaddstrcp(win, y++, x, cp, "c                        View chart mode");
  mvwaddstrcp(win, y++, x, cp, "D                        Plot historical prices");
  mvwaddstrcp(win, y++, x, cp, "d                        Download historical prices");
  mvwaddstrcp(win, y++, x, cp, "e                        View events calendar mode");
  mvwaddstrcp(win, y++, x, cp, "f                        View financials mode");
  mvwaddstrcp(win, y++, x, cp, "n                        View news mode");
  mvwaddstrcp(win, y++, x, cp, "o                        View options mode");
  mvwaddstrcp(win, y++, x, cp, "p                        View profile mode");
  mvwhline(win, y++, x, ACS_HLINE, w);
  cp = COLOR_PAIR_CMD;
  mvwaddstrcp(win, y++, x, cp, "/SYMBOL [<F(n)>] <GO>    Search symbol");
  mvwaddstrcp(win, y++, x, cp, "@VENUE <GO>              Search venue");
  mvwaddstrcp(win, y++, x, cp, ":COMMAND <GO>            Run command");

  y++;
  mvwaddstrcp(win, y++, x, cp, ":{CP [SYMBOL]+ <GO>}                    Compare prices");
  mvwaddstrcp(win, y++, x, cp, ":{CPPI <GO>}                            Consumer/Producer Price Index");
  mvwaddstrcp(win, y++, x, cp, ":{HP [DATE_RANGE [DATE_RANGE]] <GO>}    Historical prices");
  mvwaddstrcp(win, y++, x, cp, ":{SET EXPIRY [%Y-%m-%d] <GO>}           Set option series expiry date");
  mvwaddstrcp(win, y++, x, cp, ":{SET STRIKE [%f] <GO>}                 Set option series strike range");

  y++;
  mvwaddstrcp(win, y++, x, cp, "DATE_RANGE := [%Y-%m-%d, 3M, 6M, YTD, 1Y, 2Y, 5Y, 10Y, MAX]");
}

static int mvwprintq_symbol(WINDOW *win, int y, int x, const struct YQuote * const q)
{
  mvwprintwcp(win, y++, x, COLOR_PAIR_TITLE, "%s %s-%s", q->symbol, q->quoteType, q->exchange);
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, q->shortName);
  mvwprintwcp(win, y++, x, COLOR_PAIR_TITLE, "%s: %s - %s (%s)", q->fullExchangeName, q->symbol, q->quoteSourceName, q->currency);
  return y;
}

static void mvwprintq_market(WINDOW *win, int y, int x, const struct YQuote * const q,
                             double price, double change, double percent, int64_t time, const char *state)
{
  mvwprintwcp(win, y + 0, x, COLOR_PAIR_CHANGE(change), FORMAT_PRICE_CHANGE_PERCENT, price, change, percent);
  if (!IS_CLOSED(state)) {
    mvwprintwcp(win, y + 1, x, COLOR_PAIR_INFO, FORMAT_FULL_QUOTE, q->bid, q->bidSize, q->ask, q->askSize);
  }
  mvwprintw(win, y + 2, x, "%s (", strdatetime(time));
  waddstrcp(win, COLOR_PAIR_MARKET(state), state);
  waddstr(win, ")");
}

static int mvwprintq_markets(WINDOW *win, int y, int x, int w, const struct YQuote * const q)
{
  mvwprintq_market(win, y, x, q, q->regularMarketPrice, q->regularMarketChange, q->regularMarketChangePercent, q->regularMarketTime,
                   IS_REGULAR(q->marketState) ? q->marketState : "CLOSED");
  x += w;
  if (q->preMarketTime) {
    mvwprintq_market(win, y, x, q, q->preMarketPrice, q->preMarketChange, q->preMarketChangePercent, q->preMarketTime, q->marketState);
  } else if (q->postMarketTime) {
    mvwprintq_market(win, y, x, q, q->postMarketPrice, q->postMarketChange, q->postMarketChangePercent, q->postMarketTime, q->marketState);
  } else {
    mvwclrtolen(win, y + 0, x, w);
    mvwclrtolen(win, y + 1, x, w);
    mvwclrtolen(win, y + 2, x, w);
  }
  return y + 3;
}

static void wprint_quote(WINDOW *win, const struct YQuote * const q)
{
  static const struct YQuoteSummary NULL_QUOTE_SUMMARY;

  const struct YQuoteSummary *s = yql_quoteSummary_get(q->symbol);
  if (!s) {
    s = &NULL_QUOTE_SUMMARY;
  }

  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  y = mvwprintq_symbol(win, y, x, q) + 1;
  y = mvwprintq_markets(win, y, x, w / 2, q) + 1;

  int cpRegularMarketOpen    = COLOR_PAIR_CHANGE(q->regularMarketOpen    - q->regularMarketPreviousClose);
  int cpRegularMarketDayLow  = COLOR_PAIR_CHANGE(q->regularMarketDayLow  - q->regularMarketOpen);
  int cpRegularMarketDayHigh = COLOR_PAIR_CHANGE(q->regularMarketDayHigh - q->regularMarketOpen);
  int cpFiftyTwoWeekChange   = COLOR_PAIR_CHANGE(s->defaultKeyStatistics.fiftyTwoWeekChange);
  int cpFiftyTwoWeekLow      = COLOR_PAIR_CHANGE(q->fiftyTwoWeekLowChange);
  int cpFiftyTwoWeekHigh     = COLOR_PAIR_CHANGE(q->fiftyTwoWeekHighChange);
  int cpSandP52WeekChange    = COLOR_PAIR_CHANGE(s->defaultKeyStatistics.SandP52WeekChange);
  int cpFiftyDayAverage      = COLOR_PAIR_CHANGE(q->fiftyDayAverageChange);
  int cpTwoHundredDayAverage = COLOR_PAIR_CHANGE(q->twoHundredDayAverageChange);
#define _(s, f, sr, sc, fr, fc, cp, ...)                                \
  mvwprintkeyval(win, y, x, w / QUOTE_LAYOUT_COLS, s, f, sr, sc, fr, fc, cp, __VA_ARGS__);
  QUOTE_LAYOUT ;
#undef _
}

static int mvwprintp_summaryProfile(WINDOW *win, int y, int x, const struct AssetProfile * const p,
                                    const struct YQuote * const q)
{
  mvwaddstr(win, y++, x, q->longName);
  mvwaddstr(win, y++, x, p->address1);
  mvwprintw(win, y++, x, "%s, %s %s", p->city, p->state, p->zip);
  mvwaddstr(win, y++, x, p->country);
  mvwprintwcp(win, y++, x, COLOR_PAIR_LINK, "tel://%s", p->phone);
  mvwaddstrcp(win, y++, x, COLOR_PAIR_LINK, p->website);
  return y;
}

static void wprint_assetProfile(WINDOW *win, const struct AssetProfile * const p,
                                const struct YQuote * const q)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Profile");
  if (!p) {
    mvwaddstrcn(win, ++y, x, w, COLOR_PAIR_INFO, "No data found.");
    return;
  }

  ASSET_PROFILE_LAYOUT(LAYOUT_PRINT, win, y + 1, x + w / 3, w / 3, p);
  y = mvwprintp_summaryProfile(win, y + 1, x + MARGIN_X, p, q) + 1;
  COMPANY_OFFICER_LAYOUT(LAYOUT_PRINT_KEY, win, y, x + MARGIN_X, w / COMPANY_OFFICER_LAYOUT_COLS);
}

static void wprint_topHoldings(WINDOW *win, const struct TopHoldings * const p)
{
  static const struct YQuote NULL_QUOTE;

  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Top Holdings");
  if (!p) {
    mvwaddstrcn(win, ++y, x, w, COLOR_PAIR_INFO, "No data found.");
    return;
  }

  ++y, x += MARGIN_X, w /= HOLDING_LAYOUT_COLS;
  HOLDING_LAYOUT(LAYOUT_PRINT_KEY, win, y, x, w);

  int h = min(maxy - y - 1 - MARGIN_Y - 1, HOLDINGS);
  for (int i = HOLDINGS - h; i < HOLDINGS; i++, y++) {
    const struct Holding * const r = &p->holdings[i];
    const struct YQuote *q = yql_quote_get(r->symbol);
    if (!q) {
      q = &NULL_QUOTE;
    }
    int cpRow                = i % 2 ? COLOR_PAIR_YELLOW : COLOR_PAIR_BLUE;
    int cpRegularMarketPrice = COLOR_PAIR_CHANGE(q->regularMarketChange);
    HOLDING_LAYOUT(LAYOUT_PRINT_VAL, win, y, x, w, q);
  }
}

static void wprint_defaultKeyStatistics(WINDOW *win, const struct DefaultKeyStatistics * const p,
                                        const struct YQuote * const q)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Key Statistics");
  if (!p) {
    mvwaddstrcn(win, ++y, x, w, COLOR_PAIR_INFO, "No data found.");
    return;
  }

  DEFAULT_KEY_STATISTICS_LAYOUT(LAYOUT_PRINT_2, win, y + 1, x + MARGIN_X, w / DEFAULT_KEY_STATISTICS_LAYOUT_COLS, p);
}

static void wprint_chart(WINDOW *win, const struct YChart * const c)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Chart");
  if (!c || !c->count) {
    mvwaddstrcn(win, ++y, x, w, COLOR_PAIR_INFO, "No data found.");
    return;
  }

  ++y, x += MARGIN_X, w /= CHART_LAYOUT_COLS;
  CHART_LAYOUT(LAYOUT_PRINT_KEY, win, y, x, w);

  int h = min(maxy - y - 1 - MARGIN_Y - 1, c->count);
  for (size_t i = c->count - h, j = i > 0 ? i - 1 : 0; i < c->count; i++, j = i - 1, y++) {
    int cpOpen     = COLOR_PAIR_CHANGE(c->open[i]     - c->open[j]);
    int cpHigh     = COLOR_PAIR_CHANGE(c->high[i]     - c->high[j]);
    int cpLow      = COLOR_PAIR_CHANGE(c->low[i]      - c->low[j]);
    int cpClose    = COLOR_PAIR_CHANGE(c->close[i]    - c->close[j]);
    int cpAdjClose = COLOR_PAIR_CHANGE(c->adjclose[i] - c->adjclose[j]);
    int cpVolume   = COLOR_PAIR_CHANGE(c->volume[i]   - c->volume[j]);
    CHART_LAYOUT(LAYOUT_PRINT_ARR, win, y, x, w, c, i);
    mvwprintval(win, y, x, w, "%-13s", 1, 0, COLOR_PAIR_DEFAULT, strdate(c->timestamp[i]));
  }
}

static int strikeRange(const struct YOptionChain * const o, double price, int *a, int *b, int h)
{
  *a = *b = 0;
  for (int i = 0, n = o->count; i < n; i++) {
    if (o->strikes[i] > price) {
      *a = i - h / 2, *b = i + h / 2;
      if (*a < 0) {
        *b = min(*b + (0 - *a), n), *a = 0;
      }
      if (*b > n) {
        *a = max(*a - (*b - n), 0), *b = n;
      }
      break;
    }
  }
  return *a;
}

static void wprint_options(WINDOW *win, const struct YOptionChain * const o, bool straddle)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Option Chain");
  if (!o || !o->count) {
    mvwaddstrcn(win, ++y, x, w, COLOR_PAIR_INFO, "No data found.");
    return;
  }

  const struct YQuote * const q = yql_quote_get(o->underlyingSymbol);

  mvwaddstrcn(win, ++y, x + w / 3 * 0, w / 3, COLOR_PAIR_KEY , "Calls");
  mvwaddstrcn(win, y  , x + w / 3 * 1, w / 3, COLOR_PAIR_LINK, strgmdate(o->expirationDate));
  mvwaddstrcn(win, y++, x + w / 3 * 2, w / 3, COLOR_PAIR_KEY , "Puts");

  if (straddle) {
    x += MARGIN_X, w /= STRADDLE_LAYOUT_COLS;
    STRADDLE_LAYOUT(LAYOUT_PRINT_KEY, win, y, x, w);
  } else {
    x += MARGIN_X, w /= OPTION_LAYOUT_COLS;
    OPTION_LAYOUT(LAYOUT_PRINT_KEY, win, y, x, w);
  }

  int h = min(maxy - y - 1 - MARGIN_Y - 1, o->count), a = 0, b = 0;
  strikeRange(o, q->regularMarketPrice, &a, &b, h);
  for (int i = a; i < b; i++, y++) {
    const struct YOption * const call = &o->calls[i];
    const struct YOption * const put  = &o->puts[i];
    double strike = o->strikes[i];

    int cpCallChange = COLOR_PAIR_CHANGE(call->change);
    int cpCallBid    = COLOR_PAIR_CHANGE(call->bid - call->lastPrice);
    int cpCallAsk    = COLOR_PAIR_CHANGE(call->ask - call->lastPrice);
    int cpStrike     = strike <= q->regularMarketPrice ? COLOR_PAIR_INFO : COLOR_PAIR_DEFAULT;
    int cpPutChange  = COLOR_PAIR_CHANGE(put->change);
    int cpPutBid     = COLOR_PAIR_CHANGE(put->bid  - put->lastPrice);
    int cpPutAsk     = COLOR_PAIR_CHANGE(put->ask  - put->lastPrice);
    if (straddle) {
      STRADDLE_LAYOUT(LAYOUT_PRINT_VAL, win, y, x, w, true);
    } else {
      OPTION_LAYOUT(LAYOUT_PRINT_VAL, win, y, x, w, true);
    }
  }
}

static void wprint_spark(WINDOW *win, const GPtrArray * const p)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Market Overview");
  if (!p || !p->len) {
    mvwaddstrcn(win, ++y, x, w, COLOR_PAIR_INFO, "No symbols found.");
    return;
  }

  ++y, x += MARGIN_X, w /= SPARK_LAYOUT_COLS;
  SPARK_LAYOUT(LAYOUT_PRINT_KEY, win, y, x, w);
  mvwhline(win, y + 1, x, ACS_HLINE, maxx - x * 2);

  int h = min(maxy - y - 1 - MARGIN_Y - 1, p->len);
  for (guint i = p->len - h; i < p->len; i++, y++) {
    const struct YQuote * const q = yql_quote_get(g_ptr_array_index(p, i));
    if (q) {
      int cpRow                  = i % 2 ? COLOR_PAIR_YELLOW : COLOR_PAIR_BLUE;
      int cpRegularMarketPrice   = COLOR_PAIR_CHANGE(q->regularMarketChange);
      int cpRegularMarketOpen    = COLOR_PAIR_CHANGE(q->regularMarketOpen    - q->regularMarketPreviousClose);
      int cpRegularMarketDayHigh = COLOR_PAIR_CHANGE(q->regularMarketDayHigh - q->regularMarketOpen);
      int cpRegularMarketDayLow  = COLOR_PAIR_CHANGE(q->regularMarketDayLow  - q->regularMarketOpen);
      int cpFiftyTwoWeekLow      = COLOR_PAIR_CHANGE(q->fiftyTwoWeekLowChange);
      int cpFiftyTwoWeekHigh     = COLOR_PAIR_CHANGE(q->fiftyTwoWeekHighChange);
      SPARK_LAYOUT(LAYOUT_PRINT_VAR, win, y, x, w, q);
    }
  }
}

static void wprint_client(WINDOW *win, const struct Portfolio * const p)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Portfolio");
  if (!p || !p->positions || !p->positions->len) {
    mvwaddstrcn(win, ++y, x, w, COLOR_PAIR_INFO, "No portfolios found.");
    return;
  }

  ++y, x += MARGIN_X, w /= CLIENT_LAYOUT_COLS;
  CLIENT_LAYOUT(LAYOUT_PRINT_KEY, win, y, x, w);
  mvwhline(win, y + 1, x, ACS_HLINE, maxx - x * 2);

  int h = min(maxy - y - 2 - MARGIN_Y - 2, p->positions->len);
  for (guint i = p->positions->len - h; i < p->positions->len; i++, y++) {
    const struct Position * const pp = g_ptr_array_index(p->positions, i);

    int cpRow         = i % 2 ? COLOR_PAIR_YELLOW : COLOR_PAIR_BLUE;
    int cpChange      = COLOR_PAIR_CHANGE(pp->change);
    int cpTotalChange = COLOR_PAIR_CHANGE(pp->last - pp->price);
    CLIENT_LAYOUT(LAYOUT_PRINT_VAR, win, y, x, w, pp);
  }

  y += 2;
  mvwhline(win, y, x, ACS_HLINE, maxx - x * 2);
  mvwprintval(win, y, x, w, FORMAT_STRING              , 1, 0, COLOR_PAIR_KEY                        , "TOTAL");
  mvwprintval(win, y, x, w, FORMAT_PRICE               , 1, 5, COLOR_PAIR_DEFAULT                    , p->totalPrice);
  mvwprintval(win, y, x, w, FORMAT_PRICE_CHANGE        , 1, 6, COLOR_PAIR_CHANGE(p->totalDaysGain)   , p->totalDaysGain);
  mvwprintval(win, y, x, w, FORMAT_PRICE_CHANGE        , 1, 7, COLOR_PAIR_CHANGE(p->totalGain)       , p->totalGain);
  mvwprintval(win, y, x, w, FORMAT_PRICE_PERCENT_CHANGE, 1, 8, COLOR_PAIR_CHANGE(p->totalGainPercent), p->totalGainPercent);
  mvwprintval(win, y, x, w, FORMAT_PRICE               , 1, 9, COLOR_PAIR_CHANGE(p->totalValue)      , p->totalValue);
}

static void wprint_alpha(WINDOW *win, const struct Portfolio * const p)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  ALPHA_LAYOUT(LAYOUT_PRINT, win, y, x + MARGIN_X, w / ALPHA_LAYOUT_COLS, p);
}

static void wprint_summary(WINDOW *win, const struct YQuote * const q)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2;
  mvwprintq_symbol(win, y, x + w / 3 * 0, q);
  mvwprintq_markets(win, y, x + w / 3 * 1, w / 3, q);
}

static void wprinte_earnings(WINDOW *win, const struct Event * const e)
{
  static const struct YQuoteSummary NULL_QUOTE_SUMMARY;

  const struct YQuoteSummary *s = yql_quoteSummary_get(e->symbol);
  if (!s) {
    s = &NULL_QUOTE_SUMMARY;
  }

  if (!IS_PAST(e->timestamp) || IS_PAST(strpts(s->earningsTrend[0].endDate))) {
    wprintwcp(win, COLOR_PAIR_INFO, "  %+6.2f", s->earningsTrend[0].earningsEstimate.avg);
  } else {
    double epsDifference = s->earningsHistory[3].epsDifference;
    double surprisePercent = s->earningsHistory[3].surprisePercent * 100;
    wprintwcp(win, COLOR_PAIR_CHANGE(epsDifference), "  %+6.2f (%+4.f%%)", epsDifference, surprisePercent);
  }
}

static void mvwprinte_event(WINDOW *win, int y, int x, const struct Event * const e)
{
  mvwprintwcp(win, y, x, COLOR_PAIR_EVENT(e->e_evt), "%-8s %-32s", e->symbol, e->shortName);
  wprintw(win, "%s", strtime(e->timestamp));
  if (e->e_evt == EARNINGS) {
    wprinte_earnings(win, e);
  }
}

static void wprint_events(WINDOW *win, const struct EventCalendar * const c)
{
  getallyx(win);
  box(win, 0, 0);

  int y = MARGIN_Y, x = MARGIN_X, w = maxx - x * 2, cp = COLOR_PAIR_DEFAULT;
  mvwaddstrcp(win, y++, x, COLOR_PAIR_TITLE, "Events Calendar (");
  waddstrcp(win, COLOR_PAIR_CAPGAIN, "Capital Gain");
  waddstr(win, ", ");
  waddstrcp(win, COLOR_PAIR_DIVIDEND, "Dividend");
  waddstr(win, ", ");
  waddstrcp(win, COLOR_PAIR_EARNINGS, "Earnings");
  waddstr(win, ", ");
  waddstrcp(win, COLOR_PAIR_SPLIT, "Split");
  waddstr(win, ")");

  ++y, w /= 3;
  int h = maxy - y - MARGIN_Y - 1, z = 0, Z = 3 * h;
  for (int i = 0; i < EVENT_QUARTERLY && z < Z; i++) {
    const struct Event *e = c->dates[i].events;
    if (e) {
      int64_t ts = c->dates[i].date;
      cp = COLOR_PAIR_KEY | (IS_TODAY(ts) ? A_REVERSE : 0);
      mvwaddstrcp(win, y + z % h, x * 2 + z / h * w, cp, strwdate(ts));
      for (++z; e && z < Z; e = e->next, z++) {
        mvwprinte_event(win, y + z % h, x * 3 + z / h * w, e);
      }
    }
  }
}

struct Event *Event_new(enum EventType e_evt, int64_t ts, const char *symbol, const char *shortName)
{
  struct Event *e = malloc(sizeof(struct Event));
  e->next = NULL;
  e->e_evt = e_evt;
  e->timestamp = ts;
  e->symbol = symbol;
  e->shortName = shortName;
  return e;
}

void Event_delete(struct Event *e)
{
  struct Event *p = e, *q = NULL;
  while (p) {
    q = p->next;
    free(p);
    p = q;
  }
}

int Event_cmp(struct Event *p, struct Event *q)
{
  int c = 0;
  return (c = p->timestamp - q->timestamp) ? c :
    (c = strncmp(p->symbol, q->symbol, YSTRING_LENGTH)) ? c :
    (c = p->e_evt - q->e_evt);
}

int Event_add(struct Event **e, struct Event *o)
{
  if (*e) {
    struct Event *p = *e, *q = NULL;
    int c = 0;
    while (p && (c = Event_cmp(p, o)) < 0) {
      q = p, p = p->next;
    }
    if (c) {
      if (q) {
        o->next = q->next, q->next = o;
      } else {
        *e = o, o->next = p;
      }
      return 1;
    }
    /* else { */
    /*   if (q) { */
    /*     q->next = o; */
    /*   } else { */
    /*     *e = o; */
    /*   } */
    /*   o->next = p->next; */
    /*   free(p); */
    /*   return 1; */
    /* } */
  } else {
    *e = o;
    return 1;
  }
  /* free(o); */
  return 0;
}

void EventCalendar_init(struct EventCalendar *c, time_t bop, time_t eop)
{
  c->ts_bop = bop, c->ts_eop = eop;
  for (size_t i = 0; i < EVENT_QUARTERLY; i++) {
    c->dates[i].date = c->ts_bop + gtm_diffday * i;
    c->dates[i].events = NULL;
  }
}

void EventCalendar_free(struct EventCalendar *c)
{
  for (size_t i = 0; i < EVENT_QUARTERLY; i++) {
    Event_delete(c->dates[i].events);
    c->dates[i].events = NULL;
  }
}

void EventCalendar_add(struct EventCalendar *c, enum EventType e_evt, int64_t ts, const char *symbol, const char *shortName)
{
#define event_in_range(ts) (c->ts_bop <= (ts) && (ts) < c->ts_eop)
#define eventdate_index(d, ts) (d + (((ts) - c->ts_bop) / gtm_diffday))
  if (event_in_range(ts)) {
    struct Event *e = Event_new(e_evt, ts, symbol, shortName);
    struct EventDate *d = eventdate_index(c->dates, e->timestamp);
    if (!Event_add(&d->events, e)) {
      Event_delete(e);
    }
  }
}

static void addEvent(void *symbol _U_, void *quote, void *calendar)
{
  const struct YQuote * const q = quote;

  if (IS_EQUITY(q->quoteType)) {
    /* yql_earnings(q->symbol); */

    EventCalendar_add(calendar, DIVIDEND, q->dividendDate, q->symbol, q->shortName);
    EventCalendar_add(calendar, EARNINGS, q->earningsTimestamp, q->symbol, q->shortName);
    EventCalendar_add(calendar, EARNINGS, q->earningsTimestampStart, q->symbol, q->shortName);
  }
}

struct Spark *Spark_new(enum PanelType e, PANEL *pan)
{
  struct Spark *s = malloc(sizeof(struct Spark));
  s->e_pan = e;
  s->e_mod = MODE_DEFAULT;
  s->p_pan = pan;
  void Spark_init(struct Spark *);
  Spark_init(s);
  return s;
}

void Spark_init(struct Spark *s)
{
  s->cursym = g_string_sized_new(YSTRING_LENGTH);
  s->query = g_string_sized_new(YSTRING_LENGTH);
  s->startDate = DATE_RANGE_3M;
  s->endDate = DATE_RANGE_0D;
  s->events = "history";
  s->interval = "1d";
  s->range = "3mo";
  s->expiryDate = 0;
  s->strikePrice = 0.0d;

  WINDOW *p_win = s->p_pan->win;
  getallyx(p_win);

  switch (s->e_pan) {
  case HELP:
  case GOVT:
  case CORP:
  case MTGE:
  case MMKT:
  case MUNI:
  case PFD:
    break;
  case EQUITY:
#define DEFAULT_EQUITY "GME"
    s->symbols = config.g_equity;
    g_string_assign(s->cursym, s->symbols->len ? g_ptr_array_index(s->symbols, 0) : DEFAULT_EQUITY);
    for (guint i = 0; i < s->symbols->len; i++) {
      g_string_append_printf(s->query, "%s,", (char *) g_ptr_array_index(s->symbols, i));
    }

    s->w_quote         = derwin(p_win, maxy / 2, maxx / 2, 0 * maxy / 2, 0 * maxx / 2);
    s->w_options       = derwin(p_win, maxy / 2, maxx / 2, 1 * maxy / 2, 0 * maxx / 2);
    s->w_chart         = derwin(p_win, maxy / 3, maxx / 2, 0 * maxy / 3, 1 * maxx / 2);
    s->w_assetProfile  = derwin(p_win, maxy / 3, maxx / 2, 1 * maxy / 3, 1 * maxx / 2);
    s->w_keyStatistics = derwin(p_win, maxy / 3, maxx / 2, 2 * maxy / 3, 1 * maxx / 2);
    break;
  case CMDTY:
#define DEFAULT_CMDTY "GC=F"
    s->symbols = config.g_cmdty;
    g_string_assign(s->cursym, s->symbols->len ? g_ptr_array_index(s->symbols, 0) : DEFAULT_CMDTY);
    goto SPARK_INIT;
  case INDEX:
#define DEFAULT_INDEX "^GSPC"
    s->symbols = config.g_index;
    g_string_assign(s->cursym, s->symbols->len ? g_ptr_array_index(s->symbols, 0) : DEFAULT_INDEX);
    goto SPARK_INIT;
  case CRNCY:
#define DEFAULT_CRNCY "BTC-USD"
    s->symbols = config.g_crncy;
    g_string_assign(s->cursym, s->symbols->len ? g_ptr_array_index(s->symbols, 0) : DEFAULT_CRNCY);
  SPARK_INIT:
    for (guint i = 0; i < s->symbols->len; i++) {
      g_string_append_printf(s->query, "%s,", (char *) g_ptr_array_index(s->symbols, i));
    }

    s->w_quote = derwin(p_win, maxy / 2, maxx / 2, 0 * maxy / 2, 0 * maxx / 2);
    s->w_chart = derwin(p_win, maxy / 2, maxx / 2, 0 * maxy / 2, 1 * maxx / 2);
    s->w_spark = derwin(p_win, maxy / 2, maxx    , 1 * maxy / 2, 0           );
    break;
  case CLIENT:
    s->w_client = derwin(p_win, maxy - 3, maxx,        0, 0);
    s->w_alpha  = derwin(p_win,        3, maxx, maxy - 3, 0);
    break;
  default:
    break;
  }

  s->w_summary = derwin(p_win,        5, maxx, 0, 0);
  s->w_details = derwin(p_win, maxy - 5, maxx, 5, 0);
}

void Spark_free(struct Spark *s)
{
  g_string_free(s->cursym, TRUE);
  g_string_free(s->query, TRUE);

  switch (s->e_pan) {
  case HELP:
  case GOVT:
  case CORP:
  case MTGE:
  case MMKT:
  case MUNI:
  case PFD:
    break;
  case EQUITY:
    delwin(s->w_quote);
    delwin(s->w_options);
    delwin(s->w_chart);
    delwin(s->w_assetProfile);
    delwin(s->w_keyStatistics);
    break;
  case CMDTY:
  case INDEX:
  case CRNCY:
    delwin(s->w_quote);
    delwin(s->w_chart);
    delwin(s->w_spark);
    break;
  case CLIENT:
    delwin(s->w_client);
    delwin(s->w_alpha);
    break;
  default:
    break;
  }

  delwin(s->w_summary);
  delwin(s->w_details);
}

void Spark_delete(struct Spark *s)
{
  if (s) {
    Spark_free(s);
    free(s);
  }
}

static int query_e(int status, const char *s)
{
  switch (status) {
  case YERROR_NERR:
    return 0;
  case YERROR_YHOO:
    wprint_err(w_pop, &yql_error, s);
    __attribute__ ((__fallthrough__));
  default:
    return -1;
  }
}

static int query(int (*f)(const char *), const char *x)
{
  return query_e(f(x), x);
}

void Spark_update(struct Spark *s)
{
  switch (s->e_pan) {
  case HELP:
  case GOVT:
  case CORP:
  case MTGE:
  case MMKT:
  case MUNI:
  case PFD:
    break;
  case EQUITY:
    query(yql_quote, s->cursym->str);
    const struct YQuote * const q = yql_quote_get(s->cursym->str);
    if (IS_OPTION(q->quoteType)) {
      query(yql_quote, q->underlyingSymbol);
    }
    if (IS_EQUITY(q->quoteType)) {
      query(yql_quoteSummary, s->cursym->str);
      query(yql_earnings, s->cursym->str);
    }
    if (IS_ETF(q->quoteType) || IS_MUTUALFUND(q->quoteType)) {
      if (query(yql_holdings, s->cursym->str) == YERROR_NERR) {
        const struct YQuoteSummary * const qs = yql_quoteSummary_get(s->cursym->str);
        const struct Holding * const h = qs->topHoldings.holdings;
        char str[HOLDINGS * YSTRING_LENGTH + 1] = { 0 };
        for (int i = 0; i < HOLDINGS; i++) {
          YString_append(str, h[i].symbol);
          strcat(str, ",");
        }
        query(yql_quote, str);
      }
    }
    query(yql_chart, s->cursym->str);
    if (IS_EQUITY(q->quoteType) || IS_ETF(q->quoteType)) {
      if (s->expiryDate) {
        query_e(yql_options_series(s->cursym->str, s->expiryDate), s->cursym->str);
      } else if (query(yql_options, s->cursym->str) == YERROR_NERR) {
        const struct YOptionChain * const o = yql_optionChain_get(s->cursym->str);
        s->expiryDate = o->expirationDates[0];
        s->strikePrice = o->strikes[o->count / 2];
      }
    }
    if (s->query->len) {
      query(yql_quote, s->query->str);
    }
    break;
  case CMDTY:
  case INDEX:
  case CRNCY:
    query(yql_quote, s->cursym->str);
    query(yql_chart, s->cursym->str);
    if (s->query->len) {
      query(yql_quote, s->query->str);
    }
    break;
  case CLIENT:
    struct Portfolio *p = getcurrpor();
    if (p) {
      query(yql_quote, p->query->str);
      Portfolio_updates(p);
    }
    break;
  default:
    break;
  }
}

void Spark_paint(struct Spark *s)
{
  WINDOW *p_win = s->p_pan->win;
  getallyx(p_win);
  wclear(p_win);

  switch (s->e_pan) {
  case HELP:
    wprint_help(p_win);
    break;
  case GOVT:
  case CORP:
  case MTGE:
  case MMKT:
  case MUNI:
  case PFD:
    wprint_blank(p_win);
    break;
  case EQUITY:
    const struct YQuote * const q = yql_quote_get(s->cursym->str);
    const struct YQuoteSummary * const qs = yql_quoteSummary_get(s->cursym->str);
    wprint_quote(s->w_quote, q);
    if (IS_ETF(q->quoteType) || IS_MUTUALFUND(q->quoteType)) {
      wprint_topHoldings(s->w_assetProfile, qs ? &qs->topHoldings : NULL);
    } else {
      wprint_assetProfile(s->w_assetProfile, qs ? &qs->assetProfile : NULL, q);
    }
    wprint_defaultKeyStatistics(s->w_keyStatistics, qs ? &qs->defaultKeyStatistics : NULL, q);
    wprint_chart(s->w_chart, yql_chart_get(s->cursym->str));
    if (IS_OPTION(q->quoteType)) {
      wprint_quote(s->w_options, yql_quote_get(q->underlyingSymbol));
    } else {
      wprint_options(s->w_options, yql_optionChain_get(s->cursym->str), false);
    }
    break;
  case CMDTY:
  case INDEX:
  case CRNCY:
    wprint_quote(s->w_quote, yql_quote_get(s->cursym->str));
    wprint_chart(s->w_chart, yql_chart_get(s->cursym->str));
    wprint_spark(s->w_spark, s->symbols);
    break;
  case CLIENT:
    wprint_client(s->w_client, getcurrpor());
    wprint_alpha(s->w_alpha, getcurrpor());
    break;
  default:
    break;
  }

  /* touchwin(p_win); */
  /* wnoutrefresh(p_win); */
  /* update_panels(); */
}

void Spark_mpaint(struct Spark *s)
{
  if (s->e_mod == MODE_DEFAULT /* || s->e_pan < EQUITY || s->e_pan > CRNCY */) {
    Spark_paint(s);
    return;
  }

  WINDOW *p_win = s->p_pan->win;
  getallyx(p_win);
  wclear(p_win);

  wprint_summary(s->w_summary, yql_quote_get(s->cursym->str));

  switch (s->e_mod) {
    /* case MODE_DEFAULT: */
    /*   Spark_paint(s); */
    /*   break; */
  case MODE_CHART:
    wprint_chart(s->w_details, yql_chart_get(s->cursym->str));
    break;
  case MODE_OPTIONS:
    wprint_options(s->w_details, yql_optionChain_get(s->cursym->str), true);
    break;
  case MODE_EVENTS:
    yql_quote_foreach(addEvent, &calendar);
    wprint_events(s->w_details, &calendar);
    break;
  case MODE_WATCHLIST:
    wprint_spark(s->w_details, s->symbols);
    break;
  default:
    break;
  }
}

void Spark_refresh(struct Spark *s)
{
  Spark_update(s);
  Spark_mpaint(s);
}

void Spark_mrefresh(struct Spark *s, enum PanelMode e)
{
  if (s->e_pan < EQUITY || s->e_pan > CRNCY) {
    wprint_pop(w_pop, "spark", "User error", "Unsupported panel operation", s->cursym->str);
    return;
  }
  s->e_mod = e;
  Spark_refresh(s);
}

void Spark_download(struct Spark *s)
{
  if (!s->cursym->len) {
    wprint_pop(w_pop, "download", "User error", "No symbol found", s->cursym->str);
    return;
  }

#define DOWNLOAD_FILENAME "./data/hist/%s_%s_%ld_%ld.csv"
  FILE *file = mkfile(DOWNLOAD_FILENAME, s->cursym->str, s->events, s->startDate, s->endDate);
  if (!file) {
    wprint_pop(w_pop, "download", "Internal error", "File cannot be opened", s->cursym->str);
    return;
  }

  int status = yql_download_f(s->cursym->str, s->startDate, s->endDate, s->interval, file);
  switch (status) {
  case YERROR_NERR:
    wprint_pop(w_pop, "download", "Notification", "Download complete", s->cursym->str);
    break;
  case YERROR_YHOO:
    wprint_err(w_pop, &yql_error, s->cursym->str);
    break;
  default:
    wprint_pop(w_pop, "download", "Internal error", "Download failed", s->cursym->str);
    break;
  }

  fclose(file);
}

static void plot_basket(const struct YChart * const c)
{
  const struct YQuoteSummary * const qs = yql_quoteSummary_get(c->symbol);
  if (!qs) {
    return;
  }

  const struct YChart *C[HOLDINGS + 1] = { c, };
  size_t n = 1;
  for (int i = 0; i < HOLDINGS; i++) {
    const char *symbol = qs->topHoldings.holdings[i].symbol;
    if (YString_length(symbol) && query(yql_chart, symbol) == YERROR_NERR) {
      C[n++] = yql_chart_get(symbol);
    }
  }
  plt_gpplot_basket(plot, C, n);
}

static void plot_option(const struct YChart * const c)
{
  const struct YQuote * const q = yql_quote_get(c->symbol);
  const struct YChart * const d = yql_chart_get(q->underlyingSymbol);
  if (!d || !d->count) {
    wprint_pop(w_pop, "plot", "Internal error", "No underlying data found", c->symbol);
    return;
  }
  plt_gpplot_option(plot, c, d);
}

void Spark_plot(struct Spark *s)
{
  if (!s->cursym->len) {
    wprint_pop(w_pop, "plot", "User error", "No symbol found", s->cursym->str);
    return;
  }

  if (!plot) {
    wprint_pop(w_pop, "plot", "Internal error", "No plot found", s->cursym->str);
    return;
  }

  const struct YChart * const c = yql_chart_get(s->cursym->str);
  if (!c || !c->count) {
    wprint_pop(w_pop, "plot", "Internal error", "No data found", s->cursym->str);
    return;
  }

  const struct YQuote * const q = yql_quote_get(s->cursym->str);
  if (IS_ETF(q->quoteType) || IS_MUTUALFUND(q->quoteType)) {
    plot_basket(c);
  } else if (IS_OPTION(q->quoteType)) {
    plot_option(c);
  } else {
    plt_gpplot_chart(plot, c);
  }
}

void Spark_rplot(struct Spark *s, const struct YChart *C[], size_t n)
{
  if (!s->cursym->len) {
    wprint_pop(w_pop, "plot", "User error", "No symbol found", s->cursym->str);
    return;
  }

  if (!plot) {
    wprint_pop(w_pop, "plot", "Internal error", "No plot found", s->cursym->str);
    return;
  }

  const struct YChart * const c = yql_chart_get(s->cursym->str);
  if (!c || !c->count) {
    wprint_pop(w_pop, "plot", "Internal error", "No data found", s->cursym->str);
    return;
  }

  plt_gpplot_corr(plot, C, n);
}

void Spark_hplot(struct Spark *s)
{
  if (!s->cursym->len) {
    wprint_pop(w_pop, "plot", "User error", "No symbol found", s->cursym->str);
    return;
  }

  if (!plot) {
    wprint_pop(w_pop, "plot", "Internal error", "No plot found", s->cursym->str);
    return;
  }

  YArray A = { .data = NULL, .length = 0, .capacity = YARRAY_LENGTH };
  A.data = reallocarray(A.data, A.capacity, sizeof(struct YHistory));
  if (!A.data) {
    log_default("%s:%d: reallocarray(%zu): %s\n", __FILE__, __LINE__, A.capacity, strerror(errno));
    return;
  }
  int status = yql_download_h(s->cursym->str, s->startDate, s->endDate, s->interval, &A);
  status = query_e(status, s->cursym->str);
  if (status || !A.data || !A.length) {
    wprint_pop(w_pop, "plot", "Internal error", "No data found", s->cursym->str);
    return;
  }

  plt_gpplot_history(plot, &A);
  hdb_upsert_histories(&hdb, &A);

  free(A.data);
}

static void plot_series()
{
  GPtrArray *g = NULL;
  hdb_select_series(&hdb, &g);
  plt_gpplot_cppi(plot, g);
  g_ptr_array_free(g, TRUE);
}

static enum PanelType quotePanelType(const char *q, enum PanelType e)
{
  if (IS_ECNQUOTE(q) || IS_EQUITY(q) || IS_ETF(q) || IS_MUTUALFUND(q) || IS_OPTION(q)) {
    return EQUITY;
  } else if (IS_ALTSYMBOL(q) || IS_FUTURE(q)) {
    return CMDTY;
  } else if (IS_INDEX(q)) {
    return INDEX;
  } else if (IS_CRYPTO(q) || IS_CURRENCY(q)) {
    return CRNCY;
  } else {
    log_default("quotePanelType(%s)\n", q);
    return e;
  }
}

static void search(const char *symbol, enum PanelType hint)
{
  if (query(yql_quote, symbol) == 0) {
    const struct YQuote * const q = yql_quote_get(symbol);
    setcurrpan(quotePanelType(q->quoteType, hint));
    struct Spark *s = getcurrspr();
    g_string_assign(s->cursym, symbol);
  }
}

static char *getsym_eocomp(const char *str)
{
  static YString comp;
  memset(comp, 0, YSTRING_LENGTH);

  struct Spark *s = getcurrspr();

  YString symbol, expiry, clazz, strike;
  int m = sscanf(str, "%8[A-Z-]%6[0-9]%1[CP]%8[0-9]", symbol, expiry, clazz, strike);
  switch (m) {
  case -1:
  case 0:
    sprintf(comp, "%s", s->cursym->str);
    break;
  case 1:
    sprintf(comp, "%6s", strgmdate_o(s->expiryDate));
    break;
  case 2:
    sprintf(comp, "%1c", 'C');
    break;
  case 3:
    sprintf(comp, "%08.0f", s->strikePrice * 1000);
    break;
  }

  return comp;
}

static char g_string_char_at(GString *string, gssize pos)
{
  return 0 <= pos && pos < (gssize) string->len ? string->str[pos] : '\0';
}

static void wgetsym(WINDOW *win)
{
  GString *symbol = g_string_sized_new(YSTRING_LENGTH);
  enum PanelType hint = curpan;

  wprint_bot(win, TERM_PROMPT_SEARCH);
  curs_set(1);

  int c;
  while ((c = wgetch(win))) {
    switch (c) {
    case GTKEY_MMKT:
      g_string_append(symbol, "XX");
      waddstr(win, "XX");
      wrefresh(win);
      hint = MMKT;
      break;
    case GTKEY_EQUITY:
      const char *str = getsym_eocomp(symbol->str);
      g_string_append(symbol, str);
      waddstr(win, str);
      wrefresh(win);
      hint = EQUITY;
      break;
    case GTKEY_CMDTY:
      switch (g_string_char_at(symbol, symbol->len - 1)) {
      default:
        g_string_append_c(symbol, '=');
        waddch(win, '=');
        __attribute__ ((__fallthrough__));
      case '=':
        g_string_append_c(symbol, 'F');
        waddch(win, 'F');
        break;
      }
      wrefresh(win);
      hint = CMDTY;
      break;
    case GTKEY_INDEX:
      g_string_prepend_c(symbol, '^');
      getyx(win, cury, curx);
      mvwaddstr(win, cury, curx - symbol->len + 1, symbol->str);
      wrefresh(win);
      hint = INDEX;
      break;
    case GTKEY_CRNCY:
      switch (g_string_char_at(symbol, symbol->len - 1)) {
      case '-':
        g_string_append(symbol, "USD");
        waddstr(win, "USD");
        break;
      default:
        g_string_append_c(symbol, '=');
        waddch(win, '=');
        __attribute__ ((__fallthrough__));
      case '=':
        g_string_append_c(symbol, 'X');
        waddch(win, 'X');
        break;
      }
      wrefresh(win);
      hint = CRNCY;
      break;
    case GTKEY_GO:
      curs_set(0);
      search(symbol->str, hint);
      __attribute__ ((__fallthrough__));
    case GTKEY_CANCEL:
      g_string_free(symbol, TRUE);
      wprint_bot(win, TERM_PROMPT_GO);
      curs_set(0);
      wrefresh(win);
      return;
    case ASKEY_BACKSPACE:
      if (symbol->len > 0) {
        g_string_erase(symbol, symbol->len - 1, 1);
        getyx(win, cury, curx);
        mvwdelchr(win, cury, curx - 1);
        wrefresh(win);
      }
      break;
#ifdef __GNUC__
    case '-':
    case '.':
    case '0' ... '9':
    case '=':
    case 'A' ... 'Z':
    case '^':
    case 'a' ... 'z':
#else
    default:
      if (!isprint(c)) {
        break;
      }
#endif
      c = toupper(c);
      g_string_append_c(symbol, c);
      waddch(win, c);
      wrefresh(win);
      break;
    }
  }
}

static void runcmd(char *cmd)
{
  struct Spark *s = getcurrspr();

  char *tok = strtok(cmd, " ");
  if (tok) {
    time_t tm = 0;
    if (streq(tok, "CP")) {
      const struct YChart *C[HOLDINGS + 1] = { yql_chart_get(s->cursym->str), };
      size_t n = 1;
      while (n < HOLDINGS + 1 && (tok = strtok(NULL, " "))) {
        if (query(yql_chart, tok) == YERROR_NERR) {
          C[n++] = yql_chart_get(tok);
        }
      }
      Spark_rplot(s, C, n);
    } else if (streq(tok, "CPPI")) {
      plot_series();
    } else if (streq(tok, "HP")) {
      if ((tok = strtok(NULL, " "))) {
        if ((tm = strprts(tok)) == -1) {
          wprint_pop(w_pop, "rc", "User error", "Invalid start date", tok);
          return;
        }
        s->startDate = tm, s->endDate = DATE_RANGE_0D;
        if ((tok = strtok(NULL, " "))) {
          if ((tm = strprts(tok)) == -1) {
            wprint_pop(w_pop, "rc", "User error", "Invalid end date", tok);
            return;
          }
          s->endDate = tm;
        }
      }
      Spark_hplot(s);
    } else if (streq(tok, "SET")) {
      if ((tok = strtok(NULL, " "))) {
        if (streq(tok, "EXPIRY")) {
          if ((tok = strtok(NULL, " "))) {
            if ((tm = strprts(tok)) == -1) {
              wprint_pop(w_pop, "rc", "User error", "Invalid expiry date", tok);
              return;
            }
            const struct YOptionChain * const o = yql_optionChain_get(s->cursym->str);
            for (size_t i = 0; i < EXPIRATION_DATES; i++) {
              if (tm <= o->expirationDates[i]) {
                s->expiryDate = o->expirationDates[i];
                break;
              }
            }
          }
        } else if (streq(tok, "STRIKE")) {
          if ((tok = strtok(NULL, " "))) {
            s->strikePrice = atof(tok);
          }
        } else {
          wprint_pop(w_pop, "rc", "User error", "Invalid subcommand", tok);
          return;
        }
      }
    } else {
      wprint_pop(w_pop, "rc", "User error", "Invalid command", tok);
      return;
    }
  }
}

static void wgetcmd(WINDOW *win)
{
  GString *command = g_string_sized_new(YSTRING_LENGTH);

  wprint_bot(win, TERM_PROMPT_CMD);
  curs_set(1);

  int c;
  while ((c = wgetch(win))) {
    switch (c) {
    case GTKEY_GO:
      curs_set(0);
      runcmd(command->str);
      __attribute__ ((__fallthrough__));
    case GTKEY_CANCEL:
      g_string_free(command, TRUE);
      wprint_bot(win, TERM_PROMPT_GO);
      curs_set(0);
      wrefresh(win);
      return;
    case ASKEY_BACKSPACE:
      if (command->len > 0) {
        g_string_erase(command, command->len - 1, 1);
        getyx(win, cury, curx);
        mvwdelchr(win, cury, curx - 1);
        wrefresh(win);
      }
      break;
    default:
      if (isprint(c)) {
        c = toupper(c);
        g_string_append_c(command, c);
        waddch(win, c);
        wrefresh(win);
      }
      break;
    }
  }
}

static WINDOW *popwin(WINDOW *par, int d)
{
  getallyx(par);
  double h = maxy / d, w = maxx / d, m = (d - 1.0) / (2.0 * d);
  return newwin(h, w, begy + maxy * m, begx + maxx * m);
}

static WINDOW *panwin(WINDOW *par, int mary, int marx)
{
  getallyx(par);
  int h = maxy - mary * 2, w = maxx - marx * 2, y = begy + mary, x = begx + marx;
  return newwin(h, w, y, x);
}

static void init()
{
  initscr();
  curs_set(0);
  cbreak();
  noecho();
  halfdelay(1);
  keypad(stdscr, TRUE);
  nonl();
  set_escdelay(100);

  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(COLOR_BLACK, COLOR_BLACK, -1);
    init_pair(COLOR_RED, COLOR_RED, -1);
    init_pair(COLOR_GREEN, COLOR_GREEN, -1);
    init_pair(COLOR_YELLOW, COLOR_YELLOW, -1);
    init_pair(COLOR_BLUE, COLOR_BLUE, -1);
    init_pair(COLOR_MAGENTA, COLOR_MAGENTA, -1);
    init_pair(COLOR_CYAN, COLOR_CYAN, -1);
    init_pair(COLOR_WHITE, COLOR_WHITE, -1);
  }

  initts();

  getallyx(stdscr);
  w_top = subwin(stdscr, 3, maxx - MARGIN_X * 2, begy           , begx + MARGIN_X);
  w_bot = subwin(stdscr, 3, maxx - MARGIN_X * 2, begy + maxy - 3, begx + MARGIN_X);
  keypad(w_bot, TRUE);
  w_pop = popwin(stdscr, 4);
  for (enum PanelType e = HELP; e < PANEL_TYPES; e++) {
    panels[e] = new_panel(panwin(stdscr, 3, MARGIN_X));
    sparks[e] = Spark_new(e, panels[e]);
  }
}

static void paint(enum PanelType newpan)
{
  hide_panel(getcurrpan());
  setcurrpan(newpan);
  show_panel(getcurrpan());
  update_panels();
  doupdate();
}

static void start_task(void *(*task)(void *), void *arg)
{
  pthread_t thread;
  int errnum = 0;

  if ((errnum = pthread_create(&thread, NULL, task, arg)) != 0) {
    log_default("pthread_create(): %s\n", strerror(errnum));
    return;
  }
  if ((errnum = pthread_detach(thread)) != 0) {
    log_default("pthread_detach(): %s\n", strerror(errnum));
    return;
  }
}

static void start()
{
  EventCalendar_init(&calendar, gtm_bow, gtm_bow + gtm_diffday * EVENT_QUARTERLY);
#define HDB_FILENAME "./data/hist/hdb.sqlite"
  hdb_init(&hdb, HDB_FILENAME);
  hdb_open(&hdb);

  plot = plt_gpopen();
  portfolios = Portfolios_new(config.g_pfs);

  yql_init();
  yql_open();
  start_task(hdb_download_series, &hdb);

  wprint_top(w_top);
  wprint_bot(w_bot, TERM_PROMPT_GO);
  setcurrpan(HELP);
  goto START_REFRESH;

  int c;
  while ((c = getch())) {
    switch (c) {
    case GTKEY_HELP:
      setcurrpan(HELP);
      goto START_REFRESH;
    case GTKEY_GOVT:
      setcurrpan(GOVT);
      goto START_REFRESH;
    case GTKEY_CORP:
      setcurrpan(CORP);
      goto START_REFRESH;
    case GTKEY_MTGE:
      setcurrpan(MTGE);
      goto START_REFRESH;
    case GTKEY_MMKT:
      setcurrpan(MMKT);
      goto START_REFRESH;
    case GTKEY_MUNI:
      setcurrpan(MUNI);
      goto START_REFRESH;
    case GTKEY_PFD:
      setcurrpan(PFD);
      goto START_REFRESH;
    case GTKEY_EQUITY:
      setcurrpan(EQUITY);
      goto START_REFRESH;
    case GTKEY_CMDTY:
      setcurrpan(CMDTY);
      goto START_REFRESH;
    case GTKEY_INDEX:
      setcurrpan(INDEX);
      goto START_REFRESH;
    case GTKEY_CRNCY:
      setcurrpan(CRNCY);
      goto START_REFRESH;
    case GTKEY_CLIENT:
      setcurrpan(CLIENT);
    START_REFRESH:
      Spark_refresh(getcurrspr());
      paint(curpan);
      break;
    case KEY_DOWN:
    case 'j':
    case KEY_RIGHT:
    case 'l':
      setnextpan();
      goto START_REFRESH;
    case KEY_UP:
    case 'k':
    case KEY_LEFT:
    case 'h':
      setprevpan();
      goto START_REFRESH;
    case 'J':
    case 'L':
      setcurrpan(CLIENT);
      setnextpor();
      goto START_REFRESH;
    case 'H':
    case 'K':
      setcurrpan(CLIENT);
      setprevpor();
      goto START_REFRESH;
    case GTKEY_CANCEL:
      Spark_mrefresh(getcurrspr(), MODE_DEFAULT);
      paint(curpan);
      break;
    case '/':
      wgetsym(w_bot);
      goto START_REFRESH;
    case '0':
      Spark_mrefresh(getcurrspr(), MODE_WATCHLIST);
      paint(curpan);
      break;
    case '1' ... '9':
      setcurrpan(CLIENT);
      setcurrpor((guint) (c - '1'));
      goto START_REFRESH;
    case ':':
      wgetcmd(w_bot);
      goto START_REFRESH;
    case 'C':
      Spark_plot(getcurrspr());
      paint(curpan);
      break;
    case 'c':
      Spark_mrefresh(getcurrspr(), MODE_CHART);
      paint(curpan);
      break;
    case 'D':
      Spark_hplot(getcurrspr());
      paint(curpan);
      break;
    case 'd':
      Spark_download(getcurrspr());
      paint(curpan);
      break;
    case 'E':
    case 'e':
      Spark_mrefresh(getcurrspr(), MODE_EVENTS);
      paint(curpan);
      break;
    case 'O':
    case 'o':
      Spark_mrefresh(getcurrspr(), MODE_OPTIONS);
      paint(curpan);
      break;
    case 'Q':
    case 'q':
      return;
    case ERR:
      start_clock(w_top);
      doupdate();
      break;
    }
  }
}

static void destroy()
{
  yql_close();
  yql_free();

  g_ptr_array_free(portfolios, TRUE); portfolios = NULL;
  plt_gpclose(plot);                  plot = NULL;
  hdb_close(&hdb);
  EventCalendar_free(&calendar);

  delwin(w_top);                      w_top = NULL;
  delwin(w_bot);                      w_bot = NULL;
  delwin(w_pop);                      w_pop = NULL;
  for (enum PanelType e = HELP; e < PANEL_TYPES; e++) {
    Spark_delete(sparks[e]);          sparks[e] = NULL;
    hide_panel(panels[e]);
    del_panel(panels[e]);
    delwin(panels[e]->win);           panels[e] = NULL;
  }

  clear();
  refresh();
  endwin();
}

static void sighandler(int sig)
{
  switch (sig) {
  case SIGINT:
    destroy();
    iextp_config_free(&config);
    exit(EXIT_SUCCESS);
    break;
  case SIGWINCH:
    endwin();
    refresh();
    break;
  }
}

int main(int argc, char *argv[])
{
  setlocale(LC_ALL, "");

  if (signal(SIGINT, sighandler) == SIG_ERR) {
    perror("signal(SIGINT)");
  }
  if (signal(SIGWINCH, sighandler) == SIG_ERR) {
    perror("signal(SIGWINCH)");
  }

  iextp_config_open(&config, argc, argv);
  /* iextp_config_dump(&config); */

  init();
  start();
  destroy();

  iextp_config_free(&config);

  return EXIT_SUCCESS;
}
