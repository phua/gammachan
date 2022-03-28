#pragma once
#ifndef GAMMATERM_H
#define GAMMATERM_H

#include <curses.h>
#include <panel.h>
/* #include <cdk.h> */

#include <gmodule.h>

#define TERM_NAME          "Γ - ちゃん Terminal"
#define TERM_HOTKEYS       "F1: HELP  F2: GOVT  F3: CORP  F4: MTGE  F5: M-MKT  F6: MUNI  F7: PFD  F8: EQUITY  F9: CMDTY  F10: INDEX  F11: CRNCY  F12: CLIENT"
#define TERM_TIME          "%a %b %e %H:%M:%S %Y"
#define TERM_PROMPT_GO     "GO>"
#define TERM_PROMPT_SEARCH "/"
#define TERM_PROMPT_CMD    ":"
#define TERM_PROMPT_LINK   "."
#define TERM_BTN_GO        "[ GO ]"
#define TERM_BTN_CANCEL    "[ CANCEL ]"

#define ASKEY_TAB       011
#define ASKEY_ENTER     015
#define ASKEY_ESC       033
#define ASKEY_MENU      071
#define ASKEY_BACKSPACE 0177
#define ASKEY_STAB      0541

#define GTKEY_CANCEL    ASKEY_ESC
#define GTKEY_HELP      KEY_F(1)
#define GTKEY_GOVT      KEY_F(2)
#define GTKEY_CORP      KEY_F(3)
#define GTKEY_MTGE      KEY_F(4)
#define GTKEY_MMKT      KEY_F(5)
#define GTKEY_MUNI      KEY_F(6)
#define GTKEY_PFD       KEY_F(7)
#define GTKEY_EQUITY    KEY_F(8)
#define GTKEY_CMDTY     KEY_F(9)
#define GTKEY_INDEX     KEY_F(10)
#define GTKEY_CRNCY     KEY_F(11)
#define GTKEY_CLIENT    KEY_F(12)
#define GTKEY_GO        ASKEY_ENTER

#define MARGIN_Y 1
#define MARGIN_X 2

#define COLOR_PAIR_BLACK    COLOR_PAIR(COLOR_BLACK)
#define COLOR_PAIR_RED      COLOR_PAIR(COLOR_RED)
#define COLOR_PAIR_GREEN    COLOR_PAIR(COLOR_GREEN)
#define COLOR_PAIR_YELLOW   COLOR_PAIR(COLOR_YELLOW)
#define COLOR_PAIR_BLUE     COLOR_PAIR(COLOR_BLUE)
#define COLOR_PAIR_MAGENTA  COLOR_PAIR(COLOR_MAGENTA)
#define COLOR_PAIR_CYAN     COLOR_PAIR(COLOR_CYAN)
#define COLOR_PAIR_WHITE    COLOR_PAIR(COLOR_WHITE)

#define COLOR_PAIR_CANCEL   COLOR_PAIR_RED
#define COLOR_PAIR_HOTKEY   COLOR_PAIR_YELLOW
#define COLOR_PAIR_GO       COLOR_PAIR_GREEN

#define COLOR_PAIR_CMD      COLOR_PAIR_CYAN
#define COLOR_PAIR_DEFAULT  COLOR_PAIR_WHITE
#define COLOR_PAIR_ERROR    COLOR_PAIR_RED
#define COLOR_PAIR_INFO     COLOR_PAIR_YELLOW
#define COLOR_PAIR_KEY      COLOR_PAIR_MAGENTA
#define COLOR_PAIR_LINK     COLOR_PAIR_BLUE
#define COLOR_PAIR_TITLE    COLOR_PAIR_CYAN

#define COLOR_PAIR_PRE      COLOR_PAIR_YELLOW
#define COLOR_PAIR_REGULAR  COLOR_PAIR_GREEN
#define COLOR_PAIR_POST     COLOR_PAIR_YELLOW
#define COLOR_PAIR_CLOSED   COLOR_PAIR_RED

#define COLOR_PAIR_CAPGAIN  COLOR_PAIR_BLUE
#define COLOR_PAIR_DIVIDEND COLOR_PAIR_GREEN
#define COLOR_PAIR_EARNINGS COLOR_PAIR_YELLOW
#define COLOR_PAIR_SPLIT    COLOR_PAIR_RED

#define COLOR_PAIR_DOWN     COLOR_PAIR_RED
#define COLOR_PAIR_UP       COLOR_PAIR_GREEN
#define COLOR_PAIR_UNCH     COLOR_PAIR_YELLOW

#define COLOR_PAIR_TRUE     COLOR_PAIR_GREEN
#define COLOR_PAIR_FALSE    COLOR_PAIR_RED

#define COLOR_PAIR_MARKET(m)                    \
  (IS_PRE((m)) ? COLOR_PAIR_PRE :               \
   IS_REGULAR((m)) ? COLOR_PAIR_REGULAR :       \
   IS_POST((m)) ? COLOR_PAIR_POST :             \
   COLOR_PAIR_CLOSED)

#define COLOR_PAIR_EVENT(e)                     \
  ((e) == CAPGAIN ? COLOR_PAIR_CAPGAIN :        \
   (e) == DIVIDEND ? COLOR_PAIR_DIVIDEND :      \
   (e) == EARNINGS ? COLOR_PAIR_EARNINGS :      \
   (e) == SPLIT ? COLOR_PAIR_SPLIT :            \
   COLOR_PAIR_DEFAULT)

#define COLOR_PAIR_CHANGE(c)                                            \
  ((c) < 0 ? COLOR_PAIR_DOWN : (c) > 0 ? COLOR_PAIR_UP : COLOR_PAIR_UNCH)

#define COLOR_PAIR_BOOL(b)                      \
  ((b) ? COLOR_PAIR_TRUE : COLOR_PAIR_FALSE)

#define IS_PRE(m)     (strncmp(m, "PRE", 3)     == 0)
#define IS_REGULAR(m) (strncmp(m, "REGULAR", 7) == 0)
#define IS_POST(m)    (strncmp(m, "POST", 4)    == 0)
#define IS_CLOSED(m)  (strncmp(m, "CLOSED", 6)  == 0)

#define IS_ALTSYMBOL(q)   (strncmp(q, "ALTSYMBOL", 9)       == 0)
#define IS_CRYPTO(q)      (strncmp(q, "CRYPTOCURRENCY", 14) == 0)
#define IS_CURRENCY(q)    (strncmp(q, "CURRENCY", 8)        == 0)
#define IS_ECNQUOTE(q)    (strncmp(q, "ECNQUOTE", 8)        == 0)
#define IS_EQUITY(q)      (strncmp(q, "EQUITY", 6)          == 0)
#define IS_ETF(q)         (strncmp(q, "ETF", 3)             == 0)
#define IS_FUTURE(q)      (strncmp(q, "FUTURE", 6)          == 0)
#define IS_INDEX(q)       (strncmp(q, "INDEX", 5)           == 0)
#define IS_MONEYMARKET(q) (strncmp(q, "MONEYMARKET", 11)    == 0)
#define IS_MUTUALFUND(q)  (strncmp(q, "MUTUALFUND", 10)     == 0)
#define IS_NONE(q)        (strncmp(q, "NONE", 4)            == 0)
#define IS_OPTION(q)      (strncmp(q, "OPTION", 6)          == 0)

enum PanelType
{
  HELP, GOVT, CORP, MTGE, MMKT, MUNI, PFD, EQUITY, CMDTY, INDEX, CRNCY, CLIENT, PANEL_TYPES
};

enum PanelMode
{
  MODE_DEFAULT, MODE_PROFILE, MODE_FINANCIALS, MODE_CHART, MODE_OPTIONS, MODE_WATCHLIST, MODE_EVENTS, MODE_NEWS
};

struct Spark
{
  enum PanelType e_pan;
  enum PanelMode e_mod;
  PANEL  *p_pan;

  WINDOW *w_quote;
  WINDOW *w_assetProfile;
  WINDOW *w_keyStatistics;
  WINDOW *w_chart;
  WINDOW *w_options;
  WINDOW *w_spark;
  WINDOW *w_client;
  WINDOW *w_alpha;
  WINDOW *w_summary;
  WINDOW *w_details;

  struct
  {
    GString *cursym;
    GString *query;
    int64_t  startDate;
    int64_t  endDate;
    char    *events;
    char    *interval;
    char    *range;
    int64_t  expiryDate;
    double   strikePrice;

    const GPtrArray *symbols;
  };
};

struct EventCalendar
{
  int64_t ts_bop, ts_eop;

#define EVENT_QUARTERLY 90
  struct EventDate
  {
    int64_t date;

    struct Event
    {
      struct Event *next;

      enum EventType
      {
        CAPGAIN, DIVIDEND, EARNINGS, SPLIT
      } e_evt;

      int64_t timestamp;
      const char *symbol;
      const char *shortName;
    } *events;
  } dates[EVENT_QUARTERLY];
};

#endif
