/* #define _GNU_SOURCE */

#include <assert.h>

#include <curl/curl.h>
#include <gmodule.h>
#include <json-glib/json-glib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "../include/log.h"
#include "../include/yql.h"

#define _U_ __attribute__ ((__unused__))

#define LOG_FILENAME "./log/yql.log"

static Log logger = NULL;

struct YError yql_error;

GHashTable *yql_quotes = NULL;         /*< YString -> struct YQuote * */
GHashTable *yql_quoteSummaries = NULL; /*< YString -> struct YQuoteSummary * */
GHashTable *yql_charts = NULL;         /*< YString -> struct YChart * */
GHashTable *yql_optionChains = NULL;   /*< YString -> struct YOptionChain * */
GHashTable *yql_headlines = NULL;      /*< YString -> struct YHeadline * */

struct JsonBuffer
{
  char   *data;
  size_t  size;
};

static CURL *easy = NULL;
/* static char errbuf[CURL_ERROR_SIZE]; */

static int min(int x, int y)
{
  return x <= y ? x : y;
}

static int max(int x, int y)
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
        log_error(logger, "%s:%d: calloc(1, %zu): %s\n", __FILE__, __LINE__, n, strerror(errno));
        free((char *) k);
      }
    } else {
      log_error(logger, "%s:%d: strndup(%s): %s\n", __FILE__, __LINE__, k, strerror(errno));
    }
  }
  return v;
}

int YArray_resize(YArray *A, size_t size)
{
  size_t nmemb = A->capacity * 2;
  void *data = reallocarray(A->data, nmemb, size);
  if (!data) {
    log_error(logger, "%s:%d: reallocarray(%zu, %zu): %s\n", __FILE__, __LINE__, nmemb, size, strerror(errno));
    return YERROR_CERR;
  }
  A->data = data, A->capacity = nmemb;
  return YERROR_NERR;
}

void YHeadline_free(struct YHeadline *p)
{
  if (p) {
    xmlFree(p->description);
    xmlFree(p->guid);
    xmlFree(p->link);
    xmlFree(p->pubDate);
    xmlFree(p->title);
    free(p);
  }
}

void YHeadline_destroy(void *ptr)
{
  struct YHeadline *p = ptr, *q = NULL;
  while (p) {
    q = p->next;
    YHeadline_free(p);
    p = q;
  }
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

static size_t json_array(JsonReader *r, const char *n, void *v, int v0, size_t vn,
                         size_t size, void (*get)(JsonReader *, const char *, void *))
{
  size_t i = 0, j = 0, k = 0;
  if (json_reader_read_member(r, n)) {
    if (json_reader_is_array(r)) {
      if (v0 < 0) {
        k = json_reader_count_elements(r), j = k > vn ? k - vn : 0;
      } else {
        j = v0, k = min(json_reader_count_elements(r), j + vn);
      }
      for (i = j; i < k; i++) {
        if (json_reader_read_element(r, i)) {
          get(r, n, v + (i - j) * size);
        }
        json_reader_end_element(r);
      }
    }
  }
  json_reader_end_member(r);
  return i - j;
}

static size_t json_int_larray(JsonReader *r, const char *n, int64_t *v, int v0, size_t vn)
{
  return json_array(r, n, v, v0, vn, sizeof(int64_t), json_int);
}

static size_t json_double_larray(JsonReader *r, const char *n, double *v, int v0, size_t vn)
{
  return json_array(r, n, v, v0, vn, sizeof(double), json_double);
}

static size_t json_int_rarray(JsonReader *r, const char *n, int64_t *v, size_t vn)
{
  return json_array(r, n, v, -1, vn, sizeof(int64_t), json_int);
}

static size_t json_double_rarray(JsonReader *r, const char *n, double *v, size_t vn)
{
  return json_array(r, n, v, -1, vn, sizeof(double), json_double);
}

static struct YQuote *json_quote(JsonReader *r, const char *s _U_)
{
  YString symbol;
  json_string (r, "symbol", symbol);
  struct YQuote *q = ght_get(yql_quotes, symbol, sizeof(struct YQuote));

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

  /* QuoteType.OPTION */
  json_string (r, "customPriceAlertConfidence", q->customPriceAlertConfidence);
  json_int    (r, "expireDate", &q->expireDate);
  json_string (r, "expireIsoDate", q->expireIsoDate);
  json_int    (r, "openInterest", &q->openInterest);
  json_double (r, "strike", &q->strike);
  json_string (r, "underlyingSymbol", q->underlyingSymbol);

  return q;
}

static void json_companyOfficer(JsonReader *r, const char *n _U_, void *v)
{
  struct CompanyOfficer *p = (struct CompanyOfficer *) v;
  json_int    (r, "age", &p->age);
  json_int    (r, "exercisedValue", &p->exercisedValue);
  json_int    (r, "fiscalYear", &p->fiscalYear);
  json_string (r, "name", p->name);
  json_string (r, "title", p->title);
  json_int    (r, "totalPay", &p->totalPay);
  json_int    (r, "unexercisedValue", &p->unexercisedValue);
  json_int    (r, "yearBorn", &p->yearBorn);
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

    json_array  (r, "companyOfficers", p->companyOfficers, 0, COMPANY_OFFICERS,
                 sizeof(struct CompanyOfficer), json_companyOfficer);

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

static void json_calendarEvents(JsonReader *r, struct CalendarEvents *p)
{
  if (json_reader_read_member(r, "calendarEvents")) {
    json_int    (r, "dividendDate", &p->dividendDate);
    json_int    (r, "exDividendDate", &p->exDividendDate);

    if (json_reader_read_member(r, "earnings")) {
      if (json_reader_read_member(r, "earningsDate")) {
        if (json_reader_is_array(r)) {
          if (json_reader_count_elements(r) > 0) {
            json_reader_read_element(r, 0);
            json_int    (r, "raw", &p->earningsDate);
            json_reader_end_element(r);
          }
        }
      }
      json_reader_end_member(r);
    }
    json_reader_end_member(r);
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
    json_int    (r, "impliedSharesOutstanding", &p->impliedSharesOutstanding);
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

static void json_financialsChart(JsonReader *r, const char *n _U_, void *v)
{
  struct FinancialsChart *p = (struct FinancialsChart *) v;
  json_string (r, "date", p->date.str_date);
  json_int    (r, "earnings", &p->earnings);
  json_int    (r, "revenue", &p->revenue);
}

static void json_earningsHistory(JsonReader *r, const char *n _U_, void *v)
{
  struct EarningsHistory *p = (struct EarningsHistory *) v;
  json_double (r, "epsActual", &p->epsActual);
  json_double (r, "epsDifference", &p->epsDifference);
  json_double (r, "epsEstimate", &p->epsEstimate);
  json_string (r, "period", p->period);
  json_int    (r, "quarter", &p->quarter);
  json_double (r, "surprisePercent", &p->surprisePercent);
}

static void json_earningsEstimate(JsonReader *r, struct EarningsEstimate *p)
{
  if (json_reader_read_member(r, "earningsEstimate")) {
    json_double (r, "avg", &p->avg);
    json_double (r, "growth", &p->growth);
    json_double (r, "high", &p->high);
    json_double (r, "low", &p->low);
    json_int    (r, "numberOfAnalysts", &p->numberOfAnalysts);
    json_double (r, "yearAgoEps", &p->yearAgoEps);
  }
  json_reader_end_member(r);
}

static void json_revenueEstimate(JsonReader *r, struct RevenueEstimate *p)
{
  if (json_reader_read_member(r, "revenueEstimate")) {
    json_int    (r, "avg", &p->avg);
    json_double (r, "growth", &p->growth);
    json_int    (r, "high", &p->high);
    json_int    (r, "low", &p->low);
    json_int    (r, "numberOfAnalysts", &p->numberOfAnalysts);
    json_int    (r, "yearAgoRevenue", &p->yearAgoRevenue);
  }
  json_reader_end_member(r);
}

static void json_earningsTrend(JsonReader *r, const char *n _U_, void *v)
{
  struct EarningsTrend *p = (struct EarningsTrend *) v;
  json_string (r, "endDate", p->endDate);
  json_double (r, "growth", &p->growth);
  json_string (r, "period", p->period);

  json_earningsEstimate(r, &p->earningsEstimate);
  json_revenueEstimate(r, &p->revenueEstimate);
}

static void json_financialData(JsonReader *r, struct FinancialData *p)
{
  if (json_reader_read_member(r, "financialData")) {
    json_double (r, "currentPrice", &p->currentPrice);
    json_double (r, "currentRatio", &p->currentRatio);
    json_double (r, "debtToEquity", &p->debtToEquity);
    json_double (r, "earningsGrowth", &p->earningsGrowth);
    json_int    (r, "ebitda", &p->ebitda);
    json_double (r, "ebitdaMargins", &p->ebitdaMargins);
    json_string (r, "financialCurrency", p->financialCurrency);
    json_int    (r, "freeCashflow", &p->freeCashflow);
    json_double (r, "grossMargins", &p->grossMargins);
    json_int    (r, "grossProfits", &p->grossProfits);
    json_int    (r, "numberOfAnalystOpinions", &p->numberOfAnalystOpinions);
    json_int    (r, "operatingCashflow", &p->operatingCashflow);
    json_double (r, "operatingMargins", &p->operatingMargins);
    json_double (r, "profitMargins", &p->profitMargins);
    json_double (r, "quickRatio", &p->quickRatio);
    json_string (r, "recommendationKey", p->recommendationKey);
    json_double (r, "recommendationMean", &p->recommendationMean);
    json_double (r, "returnOnAssets", &p->returnOnAssets);
    json_double (r, "returnOnEquity", &p->returnOnEquity);
    json_double (r, "revenueGrowth", &p->revenueGrowth);
    json_double (r, "revenuePerShare", &p->revenuePerShare);
    json_double (r, "targetHighPrice", &p->targetHighPrice);
    json_double (r, "targetLowPrice", &p->targetLowPrice);
    json_double (r, "targetMeanPrice", &p->targetMeanPrice);
    json_double (r, "targetMedianPrice", &p->targetMedianPrice);
    json_int    (r, "totalCash", &p->totalCash);
    json_double (r, "totalCashPerShare", &p->totalCashPerShare);
    json_int    (r, "totalDebt", &p->totalDebt);
    json_int    (r, "totalRevenue", &p->totalRevenue);
  }
  json_reader_end_member(r);
}

static void json_holdings(JsonReader *r, const char *n _U_, void *v)
{
  struct Holding *p = (struct Holding *) v;
  json_string (r, "holdingName", p->holdingName);
  json_double (r, "holdingPercent", &p->holdingPercent);
  json_string (r, "symbol", p->symbol);
}

static struct YQuoteSummary *json_quoteSummary(JsonReader *r, const char *s)
{
  struct YQuoteSummary *q = ght_get(yql_quoteSummaries, s, sizeof(struct YQuoteSummary));

  json_assetProfile(r, &q->assetProfile);
  json_calendarEvents(r, &q->calendarEvents);
  json_defaultKeyStatistics(r, &q->defaultKeyStatistics);
  json_financialData(r, &q->financialData);

  if (json_reader_read_member(r, "earnings")) {
    if (json_reader_read_member(r, "financialsChart")) {
      json_array  (r, "quarterly", q->financialsChartQuarterly, 0, QUARTERLY,
                   sizeof(struct FinancialsChart), json_financialsChart);
    }
    json_reader_end_member(r);
  }
  json_reader_end_member(r);

  if (json_reader_read_member(r, "earningsHistory")) {
    json_array  (r, "history", q->earningsHistory, 0, QUARTERLY,
                 sizeof(struct EarningsHistory), json_earningsHistory);
  }
  json_reader_end_member(r);

  if (json_reader_read_member(r, "earningsTrend")) {
    json_array  (r, "trend", q->earningsTrend, 0, QUARTERLY,
                 sizeof(struct EarningsTrend), json_earningsTrend);
  }
  json_reader_end_member(r);

  if (json_reader_read_member(r, "topHoldings")) {
    json_array  (r, "holdings", q->topHoldings.holdings, 0, HOLDINGS,
                 sizeof(struct Holding), json_holdings);
  }
  json_reader_end_member(r);

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

  json_int_larray    (r, "expirationDates", o->expirationDates, 0, EXPIRATION_DATES);

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

static int json_read(JsonNode *node, const char *symbol)
{
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
          log_warn(logger, "YError: response=%s, code=%s, description=%s\n", yql_error.response, yql_error.code, yql_error.description);
          return YERROR_YHOO;
        }
      }
      json_reader_end_member(reader);

      if (json_reader_read_member(reader, "result")) {
        if (json_reader_is_array(reader)) {
          if (json_reader_count_elements(reader) == 0) {
            strncpy(yql_error.response, response, YSTRING_LENGTH);
            strncpy(yql_error.code, "YError: " YERROR_CODE, YSTRING_LENGTH);
            strncpy(yql_error.description, YERROR_DESCRIPTION, YTEXT_LENGTH);
            log_warn(logger, "YError: response=%s, code=%s, description=%s\n", yql_error.response, yql_error.code, yql_error.description);
            return YERROR_YHOO;
          }
          for (int i = 0; i < json_reader_count_elements(reader); i++) {
            if (json_reader_read_element(reader, i)) {
              if (strcmp(response, "quoteResponse") == 0) {
                json_quote(reader, symbol);
              } else if (strcmp(response, "quoteSummary") == 0) {
                json_quoteSummary(reader, symbol);
              } else if (strcmp(response, "chart") == 0) {
                json_chart(reader, symbol);
              } else if (strcmp(response, "optionChain") == 0) {
                json_optionChain(reader, symbol);
              } else {
                log_warn(logger, "YError: Unknown response=%s\n", response);
              }
            }
            json_reader_end_element(reader);
          }
        }
      }
      json_reader_end_member(reader);
    }
    json_reader_end_element(reader);
  }
  g_object_unref(reader);

  return YERROR_NERR;
}

static int json_parse(struct JsonBuffer *buffer, const char *symbol)
{
  JsonParser *parser = json_parser_new();
  GError *error = NULL;
  json_parser_load_from_data(parser, buffer->data, buffer->size, &error);
  if (error) {
    log_warn(logger, "json_parser_load_from_data(): %s\n", error->message);
    g_error_free(error);
    g_object_unref(parser);
    return YERROR_JSON;
  }
  JsonNode *root = json_parser_get_root(parser);
  log_debug(logger, "%s\n", json_to_string(root, TRUE));

  int status = json_read(root, symbol);
  g_object_unref(parser);
  return status;
}

int yql_init()
{
  log_open(&logger, LOG_FILENAME);

  yql_quotes = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
  yql_quoteSummaries = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
  yql_charts = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
  yql_optionChains = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
  yql_headlines = g_hash_table_new_full(g_str_hash, g_str_equal, free, YHeadline_destroy);

  CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
  if (code != CURLE_OK) {
    log_error(logger, "curl_global_init(CURL_GLOBAL_ALL): %s\n", curl_easy_strerror(code));
    return YERROR_CURL;
  }

  return YERROR_NERR;
}

void yql_free()
{
  xmlCleanupParser();
  curl_global_cleanup();

  g_hash_table_destroy(yql_headlines);      yql_headlines = NULL;
  g_hash_table_destroy(yql_optionChains);   yql_optionChains = NULL;
  g_hash_table_destroy(yql_charts);         yql_charts = NULL;
  g_hash_table_destroy(yql_quoteSummaries); yql_quoteSummaries = NULL;
  g_hash_table_destroy(yql_quotes);         yql_quotes = NULL;

  log_close(logger);                        logger = NULL;
}

static size_t callback(char *p, size_t size, size_t nmemb, void *user)
{
  size_t nsize = size * nmemb;
  struct JsonBuffer *buffer = (struct JsonBuffer *) user;
  buffer->data = realloc(buffer->data, buffer->size + nsize + 1);
  if (!buffer->data) {
    log_error(logger, "%s:%d: realloc(%zu): %s\n", __FILE__, __LINE__, buffer->size + nsize + 1, strerror(errno));
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
      log_error(logger, "curl_easy_init()\n");
      return YERROR_CURL;
    }
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 0L);
    /* curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf); */
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, callback);
  }
  return YERROR_NERR;
}

void yql_close()
{
  if (easy) {
    curl_easy_cleanup(easy);                easy = NULL;
  }
}

struct YQuote *yql_quote_get(const char *s)
{
  return g_hash_table_lookup(yql_quotes, s);
}

struct YQuoteSummary *yql_quoteSummary_get(const char *s)
{
  return g_hash_table_lookup(yql_quoteSummaries, s);
}

struct YChart *yql_chart_get(const char *s)
{
  return g_hash_table_lookup(yql_charts, s);
}

struct YOptionChain *yql_optionChain_get(const char *s)
{
  return g_hash_table_lookup(yql_optionChains, s);
}

struct YHeadline *yql_headline_get(const char *s)
{
  return g_hash_table_lookup(yql_headlines, s);
}

struct YHeadline *yql_headline_at(const char *s, size_t i)
{
  struct YHeadline *p = yql_headline_get(s);
  while (i-- && (p = p->next));
  return p;
}

void yql_quote_foreach(GHFunc fp, gpointer p)
{
  g_hash_table_foreach(yql_quotes, fp, p);
}

static int yql_query(const char *url, const char *symbol)
{
  log_debug(logger, "yql_query(%s)\n", url);

  curl_easy_setopt(easy, CURLOPT_URL, url);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, callback);
  struct JsonBuffer buffer = { .data = NULL, .size = 0 };
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buffer);
  curl_easy_perform(easy);

  int status = json_parse(&buffer, symbol);
  free(buffer.data);
  return status;
}

static char *yql_vasprintf(const char *fmt, va_list ap)
{
  char *str = NULL;
#ifdef _GNU_SOURCE
  vasprintf(&str, fmt, ap);
#else
  va_list args;
  va_copy(args, ap);
  int n = 0;
  n = vsnprintf(str, n, fmt, ap);
  if (n < 0) {
    log_error(logger, "vsnprintf(%s)\n", fmt);
    return NULL;
  }
  str = malloc(++n);
  if (!str) {
    log_error(logger, "%s:%d: malloc(%zu): %s\n", __FILE__, __LINE__, n, strerror(errno));
    return NULL;
  }
  n = vsnprintf(str, n, fmt, args);
  va_end(args);
  if (n < 0) {
    log_error(logger, "vsnprintf(%s)\n", fmt);
    free(str);
    return NULL;
  }
#endif
  return str;
}

static char *yql_asprintf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  char *str = yql_vasprintf(fmt, ap);
  va_end(ap);
  return str;
}

static int yql_vaquery(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  char *url = yql_vasprintf(fmt, ap);
  va_end(ap);
  if (!url) {
    log_error(logger, "yql_vasprintf(%s)\n", fmt);
    return YERROR_CERR;
  }

  va_start(ap, fmt);
  char *symbol = va_arg(ap, char *);
  va_end(ap);

  int status = yql_query(url, symbol);
  free(url);
  return status;
}

int yql_quote(const char *s)
{
  return yql_vaquery(Y_QUOTE "?symbols=%s", s);
}

int yql_quoteSummary(const char *s)
{
  return yql_vaquery(Y_QUOTESUMMARY "/%s" "?modules=assetProfile,defaultKeyStatistics,financialData", s);
}

int yql_earnings(const char *s)
{
  /* modules=indexTrend,industryTrend,sectorTrend */
  return yql_vaquery(Y_QUOTESUMMARY "/%s" "?modules=calendarEvents,earnings,earningsHistory,earningsTrend", s);
}

int yql_financials(const char *s)
{
  return yql_vaquery(Y_QUOTESUMMARY "/%s" "?modules="
                     "balanceSheetHistory,balanceSheetHistoryQuarterly,"
                     "cashflowStatementHistory,cashflowStatementHistoryQuarterly,"
                     "incomeStatementHistory,incomeStatementHistoryQuarterly",
                     s);
}

int yql_holdings(const char *s)
{
  return yql_vaquery(Y_QUOTESUMMARY "/%s" "?modules=topHoldings", s);
}

/**
 * interval := [ "2m", "1d", "1wk", "1mo" ]
 * range    := [ "1d", "5d", "1mo", "3mo", "6mo", "1y", "2y", "5y", "10y", "ytd", "max" ]
 */
int yql_chart(const char *s)
{
  const char *range = "3mo", *interval = "1d";

  return yql_vaquery(Y_CHART "/%s?symbol=%s" "&range=%s" "&interval=%s"
                     "&events=capitalGain|div|earn|split" "&includeAdjustedClose=true" "&includePrePost=true",
                     s, s, range, interval);
}

int yql_chart_range(const char *s, time_t period1, time_t period2, const char *interval)
{
  return yql_vaquery(Y_CHART "/%s?symbol=%s" "&period1=%ld" "&period2=%ld" "&interval=%s"
                     "&events=capitalGain|div|earn|split" "&includeAdjustedClose=true" "&includePrePost=true",
                     s, s, period1, period2, interval);
}

int yql_options(const char *s)
{
  return yql_vaquery(Y_OPTIONS "/%s" "?straddle=false", s);
}

int yql_options_series(const char *s, time_t date)
{
  return yql_vaquery(Y_OPTIONS "/%s" "?date=%ld" "&straddle=false", s, date);
}

int yql_options_series_k(const char *s, double strike)
{
  return yql_vaquery(Y_OPTIONS "/%s" "?strikeMin=%.2f&strikeMax=%.2f&getAllData=true" "&straddle=false", s, strike, strike);
}

/**
 * interval := [ "1d", "1wk", "1mo" ]
 * events   := [ "capitalGain", "div", "history", "split" ]
 */
static int yql_download(const char *s, time_t period1, time_t period2, const char *interval, const char *events)
{
  char *url = yql_asprintf(Y_DOWNLOAD "/%s" "?period1=%ld" "&period2=%ld" "&interval=%s" "&events=%s" "&includeAdjustedClose=true",
                           s, period1, period2, interval, events);
  if (!url) {
    log_error(logger, "yql_asprintf(%s)\n", Y_DOWNLOAD);
    return YERROR_CERR;
  }

  curl_easy_setopt(easy, CURLOPT_URL, url);
  CURLcode status = curl_easy_perform(easy);
  if (status != CURLE_OK) {
    log_warn(logger, "curl_easy_perform(%s): %s\n", url, curl_easy_strerror(status));
    free(url);
    return YERROR_CURL;
  }
  free(url);
  return YERROR_NERR;
}

static int yql_download_e(int status, char *data, size_t size)
{
  if (status != YERROR_NERR) {
    return status;
  } else if (!data || !size) {
    return YERROR_CURL;
  } else if (strncmp(data, "40", 2) == 0) {
    memset(&yql_error, 0, sizeof(struct YError));
    sscanf(data, "%" "31" "s %" "31" "[^:]: %" "127" "[^\n]", yql_error.response, yql_error.code, yql_error.description);
    log_warn(logger, "YError: response=%s, code=%s, description=%s\n", yql_error.response, yql_error.code, yql_error.description);
    return YERROR_YHOO;
  } else {
    return YERROR_NERR;
  }
}

int yql_download_r(const char *s, time_t period1, time_t period2, const char *interval, char **data, size_t *size)
{
  const char *events = "history";

  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, callback);
  struct JsonBuffer buffer = { .data = NULL, .size = 0 };
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buffer);

  int status = yql_download(s, period1, period2, interval, events);
  status = yql_download_e(status, buffer.data, buffer.size);
  if (status == YERROR_NERR) {
    *data = buffer.data, *size = buffer.size;
  } else {
    free(buffer.data);
  }
  return status;
}

static int csv_history(const char *s, const char *data, size_t size, YArray *A)
{
  size_t i = 0;
  while (i < size && data[i++] != 0x0A); /* Skip CSV header */

  const char *format = YDATE_IFORMAT ",%lf,%lf,%lf,%lf,%lf,%ld\n%n";
  for (int m = 0, n = 0; i < size; i += n) {
    if (A->length >= A->capacity) {
      int status = YArray_resize(A, sizeof(struct YHistory));
      if (status != YERROR_NERR) {
        return status;
      }
    }
    struct YHistory *h = YArray_index(A, struct YHistory, A->length);
    h->symbol = s;

    m = sscanf(data + i, format, h->date, &h->open, &h->high, &h->low, &h->close, &h->adjclose, &h->volume, &n);
    if (m < 7) {
      log_warn(logger, "sscanf(%s, %zu)\n", s, i);
      return YERROR_JSON;
    }

    A->length++;
  }

  return YERROR_NERR;
}

int yql_download_h(const char *s, time_t period1, time_t period2, const char *interval, YArray *A)
{
  char   *data = NULL; size_t  size = 0;
  int status = yql_download_r(s, period1, period2, interval, &data, &size);
  if (status == YERROR_NERR) {
    status = csv_history(s, data, size, A);
    free(data);
  }
  return status;
}

int yql_download_f(const char *s, time_t period1, time_t period2, const char *interval, FILE *fstream)
{
  const char *events = "history";

/* #ifndef WIN32 */
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, fwrite);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, fstream);
/* #endif */

  int status = yql_download(s, period1, period2, interval, events);
  char line[YTEXT_LENGTH];
  rewind(fstream);
  if (fgets(line, YTEXT_LENGTH, fstream)) {
    status = yql_download_e(status, line, strnlen(line, YTEXT_LENGTH));
  }
  return status;
}

static struct YHeadline *rss_item(xmlDoc *doc, xmlNode *node)
{
  struct YHeadline *p = calloc(1, sizeof(struct YHeadline));
  if (!p) {
    log_error(logger, "%s:%d: calloc(): %s\n", __FILE__, __LINE__, strerror(errno));
    return NULL;
  }

#define xmlCharStrEqual(s1, s2) xmlStrEqual(s1, (const xmlChar *) s2)

  for (xmlNode *n = node->xmlChildrenNode; n; n = n->next) {
    if (xmlCharStrEqual(n->name, "description")) {
      p->description = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
    } else if (xmlCharStrEqual(n->name, "guid")) {
      p->guid = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
    } else if (xmlCharStrEqual(n->name, "link")) {
      p->link = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
    } else if (xmlCharStrEqual(n->name, "pubDate")) {
      p->pubDate = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
    } else if (xmlCharStrEqual(n->name, "title")) {
      p->title = xmlNodeListGetString(doc, n->xmlChildrenNode, 1);
    }
  }

  return p;
}

static int rss_read(xmlDoc *doc, xmlNode *node, const char *s)
{
  for (xmlNode *rss = node->xmlChildrenNode; rss; rss = rss->next) {
    if (xmlCharStrEqual(rss->name, "channel")) {
      struct YHeadline *p = NULL;
      for (xmlNode *channel = rss->xmlChildrenNode; channel; channel = channel->next) {
        if (xmlCharStrEqual(channel->name, "item")) {
          struct YHeadline *q = rss_item(doc, channel);
          if (!p) {
            g_hash_table_insert(yql_headlines, strndup(s, YSTRING_LENGTH), q);
          } else {
            p->next = q;
          }
          p = q;
        }
      }
    }
  }
  return YERROR_NERR;
}

static int rss_parse(struct JsonBuffer *buffer, const char *s)
{
  xmlDoc *doc = xmlReadMemory(buffer->data, buffer->size, "noname.xml", NULL, 0);
  if (!doc) {
    log_warn(logger, "xmlReadMemory()\n");
    return YERROR_XML;
  }
  xmlNode *root = xmlDocGetRootElement(doc);
  if (!root) {
    log_warn(logger, "xmlDocGetRootElement()\n");
    return YERROR_XML;
  }
  if (!xmlCharStrEqual(root->name, "rss")) {
    log_warn(logger, "xmlDocGetRootElement(): %s\n", root->name);
    return YERROR_YHOO;
  }

  int status = rss_read(doc, root, s);
  xmlFreeDoc(doc);
  return status;
}

int yql_headline(const char *s)
{
  char *url = yql_asprintf(Y_HEADLINE "?s=%s", s);
  if (!url) {
    log_error(logger, "yql_asprintf(%s)\n", Y_HEADLINE);
    return YERROR_CERR;
  }

  curl_easy_setopt(easy, CURLOPT_URL, url);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, callback);
  struct JsonBuffer buffer = { .data = NULL, .size = 0 };
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buffer);
  CURLcode code = curl_easy_perform(easy);
  if (code != CURLE_OK) {
    log_warn(logger, "curl_easy_perform(%s): %s\n", url, curl_easy_strerror(code));
    free(url);
    return YERROR_CURL;
  }
  free(url);

  int status = rss_parse(&buffer, s);
  free(buffer.data);
  return status;
}
