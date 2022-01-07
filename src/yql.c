/* #define _GNU_SOURCE */

#include <assert.h>

#include <curl/curl.h>
#include <json-glib/json-glib.h>

#include "../include/yql.h"

struct YError yql_error;

GHashTable *yql_quotes = NULL;
GHashTable *yql_quoteSummaries = NULL;
GHashTable *yql_charts = NULL;
GHashTable *yql_optionChains = NULL;

struct JsonBuffer
{
  char  *data;
  size_t size;
};

static CURL *easy = NULL;
/* static char errbuf[CURL_ERROR_SIZE]; */
static JsonPath *path = NULL;
/* static GError *error = NULL; */

/* static */ int min(int x, int y)
{
  return x <= y ? x : y;
}

/* static */ int max(int x, int y)
{
  return x >= y ? x : y;
}

static void *ght_get(GHashTable *t, const char *k, size_t n)
{
  void *v = g_hash_table_lookup(t, k);
  if (!v) {
    if ((k = strndup(k, YSTRING_LENGTH))) {
      if ((v = calloc(1, n))) {
        g_hash_table_insert(t, (char *) k, v);
      } else {
        perror("calloc");
        free((char *) k);
      }
    } else {
      perror("strndup");
    }
  }
  return v;
}

static void json_bool(JsonReader *r, const char *n, void *v)
{
  if (json_reader_is_value(r)) {
    *((bool *) v) = !json_reader_get_null_value(r) ? json_reader_get_boolean_value(r) : false;
    return;
  } else if (json_reader_read_member(r, n)) {
    json_bool(r, "raw", v);
  } else {
    *((bool *) v) = false;
  }
  json_reader_end_member(r);
}

static void json_int(JsonReader *r, const char *n, void *v)
{
  if (json_reader_is_value(r)) {
    *((int64_t *) v) = !json_reader_get_null_value(r) ? json_reader_get_int_value(r) : 0L;
    return;
  } else if (json_reader_read_member(r, n)) {
    json_int(r, "raw", v);
  } else {
    *((int64_t *) v) = 0L;
  }
  json_reader_end_member(r);
}

static void json_double(JsonReader *r, const char *n, void *v)
{
  if (json_reader_is_value(r)) {
    *((double *) v) = !json_reader_get_null_value(r) ? json_reader_get_double_value(r) : 0.0d;
    return;
  } else if (json_reader_read_member(r, n)) {
    json_double(r, "raw", v);
  } else {
    *((double *) v) = 0.0d;
  }
  json_reader_end_member(r);
}

static size_t json_cstring(JsonReader *r, const char *n, char *v, size_t maxlen)
{
  size_t len = 0;
  if (json_reader_is_value(r)) {
    const char *s = !json_reader_get_null_value(r) ? json_reader_get_string_value(r) : "\0";
    strncpy(v, s, maxlen);
    v[maxlen - 1] = '\0';
    return strnlen(v, maxlen);
  } else if (json_reader_read_member(r, n)) {
    if ((len = json_cstring(r, "fmt", v, maxlen)) == 0) {
      len = json_cstring(r, "longFmt", v, maxlen);
    }
  } else {
    strncpy(v, "\0", maxlen);
  }
  json_reader_end_member(r);
  return len;
}

static size_t json_string(JsonReader *r, const char *n, char *v)
{
  return json_cstring(r, n, v, YSTRING_LENGTH);
}

static size_t json_text(JsonReader *r, const char *n, char *v)
{
  return json_cstring(r, n, v, YTEXT_LENGTH);
}

static size_t json_larray(JsonReader *r, const char *n, void *v, size_t v0, size_t vn,
                          void (*get)(JsonReader *, const char *, void *))
{
  static_assert(sizeof(int64_t) == sizeof(double), "sizeof(int64_t) != sizeof(double)");
#define T int64_t
  size_t i = v0, j = v0, k = v0 + vn;
  if (json_reader_read_member(r, n)) {
    if (json_reader_is_array(r)) {
      k = min(json_reader_count_elements(r), k);
      for (i = j; i < k; i++) {
        if (json_reader_read_element(r, i)) {
          get(r, n, (T *) v + (i - j));
        }
        json_reader_end_element(r);
      }
    }
  }
  json_reader_end_member(r);
  return i - j;
#undef T
}

static size_t json_double_larray(JsonReader *r, const char *n, double *v, size_t v0, size_t vn)
{
  return json_larray(r, n, v, v0, vn, json_double);
}

static size_t json_rarray(JsonReader *r, const char *n, void *v, size_t vn,
                          void (*get)(JsonReader *, const char *, void *))
{
  static_assert(sizeof(int64_t) == sizeof(double), "sizeof(int64_t) != sizeof(double)");
#define T int64_t
  size_t i = 0, j = 0, k = 0;
  if (json_reader_read_member(r, n)) {
    if (json_reader_is_array(r)) {
      k = json_reader_count_elements(r), j = k > vn ? k - vn : 0;
      for (i = j; i < k; i++) {
        if (json_reader_read_element(r, i)) {
          get(r, n, (T *) v + (i - j));
        }
        json_reader_end_element(r);
      }
    }
  }
  json_reader_end_member(r);
  return i - j;
#undef T
}

static size_t json_int_rarray(JsonReader *r, const char *n, int64_t *v, size_t vn)
{
  return json_rarray(r, n, v, vn, json_int);
}

static size_t json_double_rarray(JsonReader *r, const char *n, double *v, size_t vn)
{
  return json_rarray(r, n, v, vn, json_double);
}

static struct YQuote *json_quote(JsonReader *r, const char *s)
{
  struct YQuote *q = ght_get(yql_quotes, s, sizeof(struct YQuote));

  json_double (r, "ask", &q->ask);
  json_int    (r, "askSize", &q->askSize);
  json_string (r, "averageAnalystRating", q->averageAnalystRating);
  json_int    (r, "averageDailyVolume10Day", &q->averageDailyVolume10Day);
  json_int    (r, "averageDailyVolume3Month", &q->averageDailyVolume3Month);
  json_double (r, "bid", &q->bid);
  json_int    (r, "bidSize", &q->bidSize);
  json_double (r, "bookValue", &q->bookValue);
  json_string (r, "currency", q->currency);
  json_string (r, "displayName", q->displayName);
  json_int    (r, "dividendDate", &q->dividendDate);
  json_int    (r, "earningsTimestamp", &q->earningsTimestamp);
  json_int    (r, "earningsTimestampEnd", &q->earningsTimestampEnd);
  json_int    (r, "earningsTimestampStart", &q->earningsTimestampStart);
  json_double (r, "epsCurrentYear", &q->epsCurrentYear);
  json_double (r, "epsForward", &q->epsForward);
  json_double (r, "epsTrailingTwelveMonths", &q->epsTrailingTwelveMonths);
  json_bool   (r, "esgPopulated", &q->esgPopulated);
  json_string (r, "exchange", q->exchange);
  json_int    (r, "exchangeDataDelayedBy", &q->exchangeDataDelayedBy);
  json_string (r, "exchangeTimezoneName", q->exchangeTimezoneName);
  json_string (r, "exchangeTimezoneShortName", q->exchangeTimezoneShortName);
  json_double (r, "fiftyDayAverage", &q->fiftyDayAverage);
  json_double (r, "fiftyDayAverageChange", &q->fiftyDayAverageChange);
  json_double (r, "fiftyDayAverageChangePercent", &q->fiftyDayAverageChangePercent);
  json_double (r, "fiftyTwoWeekHigh", &q->fiftyTwoWeekHigh);
  json_double (r, "fiftyTwoWeekHighChange", &q->fiftyTwoWeekHighChange);
  json_double (r, "fiftyTwoWeekHighChangePercent", &q->fiftyTwoWeekHighChangePercent);
  json_double (r, "fiftyTwoWeekLow", &q->fiftyTwoWeekLow);
  json_double (r, "fiftyTwoWeekLowChange", &q->fiftyTwoWeekLowChange);
  json_double (r, "fiftyTwoWeekLowChangePercent", &q->fiftyTwoWeekLowChangePercent);
  json_string (r, "fiftyTwoWeekRange", q->fiftyTwoWeekRange);
  json_string (r, "financialCurrency", q->financialCurrency);
  json_int    (r, "firstTradeDateMilliseconds", &q->firstTradeDateMilliseconds);
  json_double (r, "forwardPE", &q->forwardPE);
  json_string (r, "fullExchangeName", q->fullExchangeName);
  json_int    (r, "gmtOffSetMilliseconds", &q->gmtOffSetMilliseconds);
  json_string (r, "language", q->language);
  json_string (r, "longName", q->longName);
  json_string (r, "market", q->market);
  json_int    (r, "marketCap", &q->marketCap);
  json_string (r, "marketState", q->marketState);
  json_double (r, "postMarketChange", &q->postMarketChange);
  json_double (r, "postMarketChangePercent", &q->postMarketChangePercent);
  json_double (r, "postMarketPrice", &q->postMarketPrice);
  json_int    (r, "postMarketTime", &q->postMarketTime);
  json_double (r, "preMarketChange", &q->preMarketChange);
  json_double (r, "preMarketChangePercent", &q->preMarketChangePercent);
  json_double (r, "preMarketPrice", &q->preMarketPrice);
  json_int    (r, "preMarketTime", &q->preMarketTime);
  json_double (r, "priceEpsCurrentYear", &q->priceEpsCurrentYear);
  json_int    (r, "priceHint", &q->priceHint);
  json_double (r, "priceToBook", &q->priceToBook);
  json_string (r, "quoteSourceName", q->quoteSourceName);
  json_string (r, "quoteType", q->quoteType);
  json_string (r, "region", q->region);
  json_double (r, "regularMarketChange", &q->regularMarketChange);
  json_double (r, "regularMarketChangePercent", &q->regularMarketChangePercent);
  json_double (r, "regularMarketDayHigh", &q->regularMarketDayHigh);
  json_double (r, "regularMarketDayLow", &q->regularMarketDayLow);
  json_string (r, "regularMarketDayRange", q->regularMarketDayRange);
  json_double (r, "regularMarketOpen", &q->regularMarketOpen);
  json_double (r, "regularMarketPreviousClose", &q->regularMarketPreviousClose);
  json_double (r, "regularMarketPrice", &q->regularMarketPrice);
  json_int    (r, "regularMarketTime", &q->regularMarketTime);
  json_int    (r, "regularMarketVolume", &q->regularMarketVolume);
  json_int    (r, "sharesOutstanding", &q->sharesOutstanding);
  json_string (r, "shortName", q->shortName);
  json_int    (r, "sourceInterval", &q->sourceInterval);
  json_string (r, "symbol", q->symbol);
  json_bool   (r, "tradeable", &q->tradeable);
  json_double (r, "trailingAnnualDividendRate", &q->trailingAnnualDividendRate);
  json_double (r, "trailingAnnualDividendYield", &q->trailingAnnualDividendYield);
  json_double (r, "trailingPE", &q->trailingPE);
  json_bool   (r, "triggerable", &q->triggerable);
  json_double (r, "twoHundredDayAverage", &q->twoHundredDayAverage);
  json_double (r, "twoHundredDayAverageChange", &q->twoHundredDayAverageChange);
  json_double (r, "twoHundredDayAverageChangePercent", &q->twoHundredDayAverageChangePercent);

  assert(strncmp(q->symbol, s, YSTRING_LENGTH) == 0);

  /* QuoteType.CRYPTOCURRENCY */
  json_int    (r, "circulatingSupply", &q->circulatingSupply);
  json_string (r, "fromCurrency", q->fromCurrency);
  json_string (r, "lastMarket", q->lastMarket);
  json_int    (r, "startDate", &q->startDate);
  json_string (r, "toCurrency", q->toCurrency);
  json_int    (r, "volume24Hr", &q->volume24Hr);
  json_int    (r, "volumeAllCurrencies", &q->volumeAllCurrencies);

  /* QuoteType.ETF */
  json_double (r, "trailingThreeMonthNavReturns", &q->trailingThreeMonthNavReturns);
  json_double (r, "trailingThreeMonthReturns", &q->trailingThreeMonthReturns);
  json_double (r, "ytdReturn", &q->ytdReturn);

  return q;
}

static size_t json_companyOfficers(JsonReader *r, struct CompanyOfficer *p)
{
  size_t i = 0, n = COMPANY_OFFICERS;
  if (json_reader_read_member(r, "companyOfficers")) {
    if (json_reader_is_array(r)) {
      n = min(json_reader_count_elements(r), n);
      for (i = 0; i < n; i++) {
        if (json_reader_read_element(r, i)) {
          struct CompanyOfficer *q = p + i;
          json_int    (r, "age", &q->age);
          json_int    (r, "exercisedValue", &q->exercisedValue);
          json_int    (r, "fiscalYear", &q->fiscalYear);
          json_string (r, "name", q->name);
          json_string (r, "title", q->title);
          json_int    (r, "totalPay", &q->totalPay);
          json_int    (r, "unexercisedValue", &q->unexercisedValue);
          json_int    (r, "yearBorn", &q->yearBorn);
        }
        json_reader_end_element(r);
      }
    }
  }
  json_reader_end_member(r);
  return i;
}

static void json_assetProfile(JsonReader *r, struct AssetProfile *p)
{
  if (json_reader_read_member(r, "assetProfile")) {
    json_string (r, "address1", p->address1);
    json_string (r, "address2", p->address2);
    json_string (r, "address3", p->address3);
    json_string (r, "city", p->city);
    json_string (r, "country", p->country);
    json_int    (r, "fullTimeEmployees", &p->fullTimeEmployees);
    json_string (r, "industry", p->industry);
    json_text   (r, "longBusinessSummary", p->longBusinessSummary);
    json_string (r, "phone", p->phone);
    json_string (r, "sector", p->sector);
    json_string (r, "state", p->state);
    json_string (r, "website", p->website);
    json_string (r, "zip", p->zip);

    json_companyOfficers(r, p->companyOfficers);

    json_int    (r, "auditRisk", &p->auditRisk);
    json_int    (r, "boardRisk", &p->boardRisk);
    json_int    (r, "compensationAsOfEpochDate", &p->compensationAsOfEpochDate);
    json_int    (r, "compensationRisk", &p->compensationRisk);
    json_int    (r, "governanceEpochDate", &p->governanceEpochDate);
    json_int    (r, "overallRisk", &p->overallRisk);
    json_int    (r, "shareHolderRightsRisk", &p->shareHolderRightsRisk);
  }
  json_reader_end_member(r);
}

static void json_defaultKeyStatistics(JsonReader *r, struct DefaultKeyStatistics *p)
{
  if (json_reader_read_member(r, "defaultKeyStatistics")) {
    json_double (r, "annualHoldingsTurnover", &p->annualHoldingsTurnover);
    json_double (r, "annualReportExpenseRatio", &p->annualReportExpenseRatio);
    json_double (r, "beta", &p->beta);
    json_double (r, "beta3Year", &p->beta3Year);
    json_double (r, "bookValue", &p->bookValue);
    json_string (r, "category", p->category);
    json_int    (r, "dateShortInterest", &p->dateShortInterest);
    json_double (r, "earningsQuarterlyGrowth", &p->earningsQuarterlyGrowth);
    json_double (r, "enterpriseToEbitda", &p->enterpriseToEbitda);
    json_double (r, "enterpriseToRevenue", &p->enterpriseToRevenue);
    json_int    (r, "enterpriseValue", &p->enterpriseValue);
    json_double (r, "52WeekChange", &p->fiftyTwoWeekChange);
    json_double (r, "fiveYearAverageReturn", &p->fiveYearAverageReturn);
    json_int    (r, "floatShares", &p->floatShares);
    json_double (r, "forwardEps", &p->forwardEps);
    json_double (r, "forwardPE", &p->forwardPE);
    json_string (r, "fundFamily", p->fundFamily);
    json_int    (r, "fundInceptionDate", &p->fundInceptionDate);
    json_double (r, "heldPercentInsiders", &p->heldPercentInsiders);
    json_double (r, "heldPercentInstitutions", &p->heldPercentInstitutions);
    json_double (r, "impliedSharesOutstanding", &p->impliedSharesOutstanding);
    json_double (r, "lastCapGain", &p->lastCapGain);
    json_int    (r, "lastDividendDate", &p->lastDividendDate);
    json_double (r, "lastDividendValue", &p->lastDividendValue);
    json_int    (r, "lastFiscalYearEnd", &p->lastFiscalYearEnd);
    json_int    (r, "lastSplitDate", &p->lastSplitDate);
    json_string (r, "lastSplitFactor", p->lastSplitFactor);
    json_string (r, "legalType", p->legalType);
    json_int    (r, "morningStarOverallRating", &p->morningStarOverallRating);
    json_int    (r, "morningStarRiskRating", &p->morningStarRiskRating);
    json_int    (r, "mostRecentQuarter", &p->mostRecentQuarter);
    json_int    (r, "netIncomeToCommon", &p->netIncomeToCommon);
    json_int    (r, "nextFiscalYearEnd", &p->nextFiscalYearEnd);
    json_double (r, "pegRatio", &p->pegRatio);
    json_int    (r, "priceHint", &p->priceHint);
    json_double (r, "priceToBook", &p->priceToBook);
    json_double (r, "priceToSalesTrailing12Months", &p->priceToSalesTrailing12Months);
    json_double (r, "profitMargins", &p->profitMargins);
    json_double (r, "revenueQuarterlyGrowth", &p->revenueQuarterlyGrowth);
    json_double (r, "SandP52WeekChange", &p->SandP52WeekChange);
    json_int    (r, "sharesOutstanding", &p->sharesOutstanding);
    json_double (r, "sharesPercentSharesOut", &p->sharesPercentSharesOut);
    json_int    (r, "sharesShort", &p->sharesShort);
    json_int    (r, "sharesShortPreviousMonthDate", &p->sharesShortPreviousMonthDate);
    json_int    (r, "sharesShortPriorMonth", &p->sharesShortPriorMonth);
    json_double (r, "shortPercentOfFloat", &p->shortPercentOfFloat);
    json_double (r, "shortRatio", &p->shortRatio);
    json_double (r, "threeYearAverageReturn", &p->threeYearAverageReturn);
    json_int    (r, "totalAssets", &p->totalAssets);
    json_double (r, "trailingEps", &p->trailingEps);
    json_double (r, "yield", &p->yield);
    json_double (r, "ytdReturn", &p->ytdReturn);
  }
  json_reader_end_member(r);
}

static struct YQuoteSummary *json_quoteSummary(JsonReader *r, const char *s)
{
  struct YQuoteSummary *q = ght_get(yql_quoteSummaries, s, sizeof(struct YQuoteSummary));

  json_assetProfile(r, &q->assetProfile);
  json_defaultKeyStatistics(r, &q->defaultKeyStatistics);

  return q;
}

static struct YChart *json_chart(JsonReader *r, const char *s)
{
  struct YChart *c = ght_get(yql_charts, s, sizeof(struct YChart));

  if (json_reader_read_member(r, "meta")) {
    json_double (r, "chartPreviousClose", &c->chartPreviousClose);
    json_double (r, "regularMarketPrice", &c->regularMarketPrice);
    json_string (r, "symbol", c->symbol);
    assert(strncmp(c->symbol, s, YSTRING_LENGTH) == 0);
  }
  json_reader_end_member(r);

  c->count = json_int_rarray    (r, "timestamp", c->timestamp, YARRAY_LENGTH);

  if (json_reader_read_member(r, "indicators")) {
    if (json_reader_read_member(r, "adjclose")) {
      if (json_reader_is_array(r)) {
        for (int i = 0; i < json_reader_count_elements(r); i++) {
          if (json_reader_read_element(r, i)) {
            assert(json_double_rarray (r, "adjclose", c->adjclose, YARRAY_LENGTH) == c->count);
          }
          json_reader_end_element(r);
        }
      }
    }
    json_reader_end_member(r);

    if (json_reader_read_member(r, "quote")) {
      if (json_reader_is_array(r)) {
        for (int i = 0; i < json_reader_count_elements(r); i++) {
          if (json_reader_read_element(r, i)) {
            assert(json_double_rarray (r, "close", c->close, YARRAY_LENGTH)       == c->count);
            assert(json_double_rarray (r, "high", c->high, YARRAY_LENGTH)         == c->count);
            assert(json_double_rarray (r, "low", c->low, YARRAY_LENGTH)           == c->count);
            assert(json_double_rarray (r, "open", c->open, YARRAY_LENGTH)         == c->count);
            assert(json_int_rarray    (r, "volume", c->volume, YARRAY_LENGTH)     == c->count);
          }
          json_reader_end_element(r);
        }
      }
    }
    json_reader_end_member(r);
  }
  json_reader_end_member(r);

  return c;
}

static void json_option(JsonReader *r, struct YOption *p)
{
  json_double (r, "ask", &p->ask);
  json_double (r, "bid", &p->bid);
  json_double (r, "change", &p->change);
  json_string (r, "contractSize", p->contractSize);
  json_string (r, "contractSymbol", p->contractSymbol);
  json_string (r, "currency", p->currency);
  json_int    (r, "expiration", &p->expiration);
  json_double (r, "impliedVolatility", &p->impliedVolatility);
  json_bool   (r, "inTheMoney", &p->inTheMoney);
  json_double (r, "lastPrice", &p->lastPrice);
  json_int    (r, "lastTradeDate", &p->lastTradeDate);
  json_int    (r, "openInterest", &p->openInterest);
  json_double (r, "percentChange", &p->percentChange);
  json_double (r, "strike", &p->strike);
  json_int    (r, "volume", &p->volume);
}

static size_t json_options(JsonReader *r, const char *n, struct YOption *p,
                           const double ks[], size_t kn)
{
  size_t ki = 0;
  if (json_reader_read_member(r, n)) {
    if (json_reader_is_array(r)) {
      for (int i = 0; i < json_reader_count_elements(r); i++) {
        if (json_reader_read_element(r, i)) {
          double k = 0.0d;
          json_double (r, "strike", &k);
          for ( ; ki < kn && ks[ki] <= k; ki++, p++) {
            if (ks[ki] == k) {
              json_option(r, p);
            }
          }
        }
        json_reader_end_element(r);
      }
    }
  }
  json_reader_end_member(r);
  return ki;
}

static void json_straddle(JsonReader *r, struct YOption *p, struct YOption *q)
{
  if (json_reader_read_member(r, "call")) {
    json_option(r, p);
  }
  json_reader_end_member(r);

  if (json_reader_read_member(r, "put")) {
    json_option(r, q);
  }
  json_reader_end_member(r);
}

static size_t json_straddles(JsonReader *r, struct YOption *p, struct YOption *q,
                             const double ks[], size_t kn)
{
  size_t ki = 0;
  if (json_reader_read_member(r, "straddles")) {
    if (json_reader_is_array(r)) {
      for (int i = 0; i < json_reader_count_elements(r); i++) {
        if (json_reader_read_element(r, i)) {
          double k = 0.0d;
          json_double (r, "strike", &k);
          for ( ; ki < kn && ks[ki] <= k; ki++, p++, q++) {
            if (ks[ki] == k) {
              json_straddle(r, p, q);
            }
          }
        }
        json_reader_end_element(r);
      }
    }
  }
  json_reader_end_member(r);
  return ki;
}

static struct YOptionChain *json_optionChain(JsonReader *r, const char *s)
{
  struct YOptionChain *o = ght_get(yql_optionChains, s, sizeof(struct YOptionChain));

  json_string (r, "underlyingSymbol", o->underlyingSymbol);
  assert(strncmp(o->underlyingSymbol, s, YSTRING_LENGTH) == 0);

  struct YQuote *q = ght_get(yql_quotes, o->underlyingSymbol, sizeof(struct YQuote));
  /* if (json_reader_read_member(r, "quote")) { */
  /*   q = json_quote(r, o->underlyingSymbol); */
  /* } */
  /* json_reader_end_member(r); */

  int strikeRange(JsonReader *r, int *a, int *b, size_t n)
  {
    *a = *b = 0;
    if (json_reader_read_member(r, "strikes")) {
      if (json_reader_is_array(r)) {
        for (int i = 0, k = json_reader_count_elements(r); i < k; i++) {
          if (json_reader_read_element(r, i)) {
            double strike = 0.0d;
            json_double (r, "strike", &strike);
            if (strike > q->regularMarketPrice) {
              *a = i - n / 2, *b = i + n / 2;
              if (*a < 0) {
                *b = min(*b + (0 - *a), k), *a = max(0, *a);
              }
              if (*b > k) {
                *a = max(*a - (*b - k), 0), *b = min(*b, k);
              }
              json_reader_end_element(r);
              break;
            }
          }
          json_reader_end_element(r);
        }
      }
    }
    json_reader_end_member(r);
    return *a;
  }
  int a = 0, b = 0;
  strikeRange(r, &a, &b, YARRAY_LENGTH);
  o->count = json_double_larray (r, "strikes", o->strikes, a, YARRAY_LENGTH);

  if (json_reader_read_member(r, "options")) {
    if (json_reader_is_array(r)) {
      for (int i = 0; i < json_reader_count_elements(r); i++) {
        if (json_reader_read_element(r, i)) {
          json_int    (r, "expirationDate", &o->expirationDate);
          json_bool   (r, "hasMiniOptions", &o->hasMiniOptions);
          json_options(r, "calls", o->calls, o->strikes, o->count);
          json_options(r, "puts", o->puts, o->strikes, o->count);
          json_straddles(r, o->calls, o->puts, o->strikes, o->count);
        }
        json_reader_end_element(r);
      }
    }
  }
  json_reader_end_member(r);

  return o;
}

static int json_read(JsonNode *node)
{
  static YString symbol = "";

  JsonReader *reader = json_reader_new(node);
  if (json_reader_is_object(reader)) {
    if (json_reader_read_element(reader, 0)) {
      const char *response = json_reader_get_member_name(reader);

      if (json_reader_read_member(reader, "error")) {
        if (!json_reader_get_null_value(reader)) {
          strncpy(yql_error.response, response, YSTRING_LENGTH);
          if (json_reader_read_member(reader, "code")) {
            if (!json_reader_get_null_value(reader)) {
              strncpy(yql_error.code, json_reader_get_string_value(reader), YSTRING_LENGTH);
            }
          }
          json_reader_end_member(reader);
          if (json_reader_read_member(reader, "description")) {
            if (!json_reader_get_null_value(reader)) {
              strncpy(yql_error.description, json_reader_get_string_value(reader), YTEXT_LENGTH);
            }
          }
          json_reader_end_member(reader);
          fprintf(stderr, "YError: response=%s, code=%s, description=%s\n", yql_error.response, yql_error.code, yql_error.description);
          return -1;
        }
      }
      json_reader_end_member(reader);

      if (json_reader_read_member(reader, "result")) {
        if (json_reader_is_array(reader)) {
          if (json_reader_count_elements(reader) == 0) {
            strncpy(yql_error.response, response, YSTRING_LENGTH);
            strncpy(yql_error.code, "YError: " YERROR_CODE, YSTRING_LENGTH);
            strncpy(yql_error.description, YERROR_DESCRIPTION, YTEXT_LENGTH);
            fprintf(stderr, "YError: response=%s, code=%s, description=%s\n", yql_error.response, yql_error.code, yql_error.description);
            return -1;
          }
          JsonNode *match = json_path_match(path, node);
          /* assert(json_node_get_node_type(match) == JSON_NODE_ARRAY); */
          JsonArray *matches = json_node_get_array(match);
          for (int i = 0; i < json_reader_count_elements(reader); i++) {
            if (json_reader_read_element(reader, i)) {
              if (i < (int) json_array_get_length(matches)) {
                strncpy(symbol, json_array_get_string_element(matches, i), YSTRING_LENGTH);
              }
              if (strcmp(response, "quoteResponse") == 0) {
                json_quote(reader, symbol);
              } else if (strcmp(response, "quoteSummary") == 0) {
                json_quoteSummary(reader, symbol);
              } else if (strcmp(response, "chart") == 0) {
                json_chart(reader, symbol);
              } else if (strcmp(response, "optionChain") == 0) {
                json_optionChain(reader, symbol);
              } else {
                fprintf(stderr, "YError: Unknown response=%s\n", response);
              }
            }
            json_reader_end_element(reader);
          }
          json_node_unref(match);
        }
      }
      json_reader_end_member(reader);
    }
    json_reader_end_element(reader);
  }
  g_object_unref(reader);

  return 0;
}

static int json_parse(struct JsonBuffer *buffer)
{
  JsonParser *parser = json_parser_new();
  GError *error = NULL;
  json_parser_load_from_data(parser, buffer->data, buffer->size, &error);
  if (error) {
    fprintf(stderr, "json_parser_load_from_data(): %s\n", error->message);
    gint code = error->code;
    g_error_free(error);
    g_object_unref(parser);
    return code;
  }
  JsonNode *root = json_parser_get_root(parser);
  fprintf(stderr, "%s\n", json_to_string(root, TRUE));

  int status = json_read(root);
  g_object_unref(parser);
  return status;
}

int yql_init()
{
  yql_quotes = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
  yql_quoteSummaries = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
  yql_charts = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
  yql_optionChains = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);

  CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
  if (code != CURLE_OK) {
    fprintf(stderr, "curl_global_init(CURL_GLOBAL_ALL): %s\n", curl_easy_strerror(code));
    return code;
  }

  path = json_path_new();
  json_path_compile(path, "$..symbol", NULL);

  return 0;
}

void yql_free()
{
  g_object_unref(path);                     path = NULL;

  curl_global_cleanup();

  g_hash_table_destroy(yql_optionChains);   yql_optionChains = NULL;
  g_hash_table_destroy(yql_charts);         yql_charts = NULL;
  g_hash_table_destroy(yql_quoteSummaries); yql_quoteSummaries = NULL;
  g_hash_table_destroy(yql_quotes);         yql_quotes = NULL;
}

static size_t callback(char *p, size_t size, size_t nmemb, void *user)
{
  size_t nsize = size * nmemb;
  struct JsonBuffer *buffer = (struct JsonBuffer *) user;
  buffer->data = realloc(buffer->data, buffer->size + nsize + 1);
  if (!buffer->data) {
    perror("realloc");
    return 0;
  }
  memcpy(&buffer->data[buffer->size], p, nsize);
  buffer->size += nsize;
  buffer->data[buffer->size] = 0;
  return nsize;
}

int yql_open()
{
  if (!easy) {
    easy = curl_easy_init();
    if (!easy) {
      fprintf(stderr, "curl_easy_init()\n");
      return -1;
    }
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
    /* curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf); */
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, callback);
  }
  return 0;
}

void yql_close()
{
  if (easy) {
    curl_easy_cleanup(easy);                easy = NULL;
  }
}

static int yql_query(const char *url)
{
  curl_easy_setopt(easy, CURLOPT_URL, url);
  struct JsonBuffer buffer = { .data = NULL, .size = 0 };
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buffer);
  curl_easy_perform(easy);

  int status = json_parse(&buffer);
  free(buffer.data);
  return status;
}

static int yql_vaquery(const char *fmt, ...)
{
  char *url = NULL;
  va_list ap;
  va_start(ap, fmt);
#ifdef _GNU_SOURCE
  vasprintf(&url, fmt, ap);
#else
  int n = 0;
  n = vsnprintf(url, n, fmt, ap);
  va_end(ap);
  if (n < 0) {
    fprintf(stderr, "vsnprintf(%s)\n", fmt);
    return n;
  }
  url = malloc(++n);
  if (!url) {
    perror("malloc");
    return -1;
  }
  va_start(ap, fmt);
  n = vsnprintf(url, n, fmt, ap);
  if (n < 0) {
    fprintf(stderr, "vsnprintf(%s)\n", fmt);
    va_end(ap);
    free(url);
    return n;
  }
#endif
  va_end(ap);

  int status = yql_query(url);
  free(url);
  return status;
}

int yql_quote(const char *s)
{
  return yql_vaquery(Y_QUOTE "?symbols=%s", s);
}

int yql_quoteSummary(const char *s)
{
  return yql_vaquery(Y_QUOTESUMMARY "/%s" "?modules=assetProfile,defaultKeyStatistics", s);
}

int yql_chart(const char *s)
{
  /* interval := [ "2m", "1d", "1wk", "1mo" ] */
  /* range    := [ "1d", "5d", "1mo", "3mo", "6mo", "1y", "2y", "5y", "10y", "ytd", "max" ] */
  return yql_vaquery(Y_CHART "/%s?symbol=%s"
                     "&events=div|split|earn" "&includeAdjustedClose=true" "&includePrePost=true"
                     "&interval=%s&range=%s"
                     "&useYfid=true",
                     s, s, "1d", "3mo");
}

int yql_options(const char *s)
{
  return yql_vaquery(Y_OPTIONS "/%s" "?straddle=true", s);
}
