#pragma once
#ifndef Y_FINANCE_H
#define Y_FINANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define Y_HOST1        "https://query1.finance.yahoo.com/"
#define Y_HOST2        "https://query2.finance.yahoo.com/"
#define Y_SPARK        Y_HOST1 "v7/finance/spark"
#define Y_QUOTE        Y_HOST1 "v7/finance/quote"
#define Y_QUOTESUMMARY Y_HOST1 "v10/finance/quoteSummary"
#define Y_CHART        Y_HOST1 "v8/finance/chart"
#define Y_OPTIONS      Y_HOST1 "v7/finance/options"
#define Y_DOWNLOAD     Y_HOST1 "v7/finance/download"
#define Y_INSIGHTS     Y_HOST1 "ws/insights/v2/finance/insights"
#define Y_TIMESERIES   Y_HOST1 "ws/fundamentals-timeseries/v1/finance/timeseries"

#define YARRAY_LENGTH  64
#define YSTRING_LENGTH 32
#define YTEXT_LENGTH   128

/* typedef struct YArray */
/* { */
/*   void  *data; */
/*   size_t size; */
/* } YArray; */

typedef char YString[YSTRING_LENGTH];
typedef char YText[YTEXT_LENGTH];

#define YERROR_CODE        "Not Found"
#define YERROR_DESCRIPTION "No data found, symbol may be delisted"

typedef enum YErrorCode
{
  YERROR_NERR = 0, YERROR_CERR, YERROR_CURL, YERROR_JSON, YERROR_YHOO,
} YErrorCode;

struct YError
{
  YString response;
  YString code;
  YText   description;
};

struct YQuote
{
  double  ask;
  int64_t askSize;
  YString averageAnalystRating;
  int64_t averageDailyVolume10Day;
  int64_t averageDailyVolume3Month;
  double  bid;
  int64_t bidSize;
  double  bookValue;
  YString currency;
  YString displayName;
  int64_t dividendDate;
  int64_t earningsTimestamp;
  int64_t earningsTimestampEnd;
  int64_t earningsTimestampStart;
  double  epsCurrentYear;
  double  epsForward;
  double  epsTrailingTwelveMonths;
  bool    esgPopulated;
  YString exchange;
  int64_t exchangeDataDelayedBy;
  YString exchangeTimezoneName;
  YString exchangeTimezoneShortName;
  double  fiftyDayAverage;
  double  fiftyDayAverageChange;
  double  fiftyDayAverageChangePercent;
  double  fiftyTwoWeekHigh;
  double  fiftyTwoWeekHighChange;
  double  fiftyTwoWeekHighChangePercent;
  double  fiftyTwoWeekLow;
  double  fiftyTwoWeekLowChange;
  double  fiftyTwoWeekLowChangePercent;
  YString fiftyTwoWeekRange;
  YString financialCurrency;
  int64_t firstTradeDateMilliseconds;
  double  forwardPE;
  YString fullExchangeName;
  int64_t gmtOffSetMilliseconds;
  YString language;
  YString longName;
  YString market;
  int64_t marketCap;
  YString marketState;
  double  postMarketChange;
  double  postMarketChangePercent;
  double  postMarketPrice;
  int64_t postMarketTime;
  double  preMarketChange;
  double  preMarketChangePercent;
  double  preMarketPrice;
  int64_t preMarketTime;
  double  priceEpsCurrentYear;
  int64_t priceHint;
  double  priceToBook;
  YString quoteSourceName;
  YString quoteType;
  YString region;
  double  regularMarketChange;
  double  regularMarketChangePercent;
  double  regularMarketDayHigh;
  double  regularMarketDayLow;
  YString regularMarketDayRange;
  double  regularMarketOpen;
  double  regularMarketPreviousClose;
  double  regularMarketPrice;
  int64_t regularMarketTime;
  int64_t regularMarketVolume;
  int64_t sharesOutstanding;
  YString shortName;
  int64_t sourceInterval;
  YString symbol;
  bool    tradeable;
  double  trailingAnnualDividendRate;
  double  trailingAnnualDividendYield;
  double  trailingPE;
  bool    triggerable;
  double  twoHundredDayAverage;
  double  twoHundredDayAverageChange;
  double  twoHundredDayAverageChangePercent;

  /* QuoteType.CRYPTOCURRENCY */
  int64_t circulatingSupply;
  YString fromCurrency;
  YString lastMarket;
  int64_t startDate;
  YString toCurrency;
  int64_t volume24Hr;
  int64_t volumeAllCurrencies;

  /* QuoteType.ETF */
  double  trailingThreeMonthNavReturns;
  double  trailingThreeMonthReturns;
  double  ytdReturn;
};

struct YQuoteSummary
{
  struct AssetProfile
  {
    YString address1;
    YString address2;
    YString address3;
    YString city;
    YString country;
    int64_t fullTimeEmployees;
    YString industry;
    YText   longBusinessSummary;
    YString phone;
    YString sector;
    YString state;
    YString website;
    YString zip;

#define COMPANY_OFFICERS 0
    struct CompanyOfficer
    {
      int64_t age;
      int64_t exercisedValue;
      int64_t fiscalYear;
      YString name;
      YString title;
      int64_t totalPay;
      int64_t unexercisedValue;
      int64_t yearBorn;
    } companyOfficers[COMPANY_OFFICERS];

    int64_t auditRisk;
    int64_t boardRisk;
    int64_t compensationAsOfEpochDate;
    int64_t compensationRisk;
    int64_t governanceEpochDate;
    int64_t overallRisk;
    int64_t shareHolderRightsRisk;
  } assetProfile;

  struct CalendarEvents
  {
    int64_t dividendDate;
    int64_t exDividendDate;

    /* struct Earnings */
    /* { */
    /*   double  earningsAverage; */
    int64_t earningsDate;
    /*   double  earningsHigh; */
    /*   double  earningsLow; */
    /*   int64_t revenueAverage; */
    /*   int64_t revenueHigh; */
    /*   int64_t revenueLow; */
    /* } earnings; */
  } calendarEvents;

  struct DefaultKeyStatistics
  {
    double  annualHoldingsTurnover;
    double  annualReportExpenseRatio;
    double  beta;
    double  beta3Year;
    double  bookValue;
    YString category;
    int64_t dateShortInterest;
    double  earningsQuarterlyGrowth;
    double  enterpriseToEbitda;
    double  enterpriseToRevenue;
    int64_t enterpriseValue;
    double  fiftyTwoWeekChange;
    double  fiveYearAverageReturn;
    int64_t floatShares;
    double  forwardEps;
    double  forwardPE;
    YString fundFamily;
    int64_t fundInceptionDate;
    double  heldPercentInsiders;
    double  heldPercentInstitutions;
    int64_t impliedSharesOutstanding;
    double  lastCapGain;
    int64_t lastDividendDate;
    double  lastDividendValue;
    int64_t lastFiscalYearEnd;
    int64_t lastSplitDate;
    YString lastSplitFactor;
    YString legalType;
    int64_t morningStarOverallRating;
    int64_t morningStarRiskRating;
    int64_t mostRecentQuarter;
    int64_t netIncomeToCommon;
    int64_t nextFiscalYearEnd;
    double  pegRatio;
    int64_t priceHint;
    double  priceToBook;
    double  priceToSalesTrailing12Months;
    double  profitMargins;
    double  revenueQuarterlyGrowth;
    double  SandP52WeekChange;
    int64_t sharesOutstanding;
    double  sharesPercentSharesOut;
    int64_t sharesShort;
    int64_t sharesShortPreviousMonthDate;
    int64_t sharesShortPriorMonth;
    double  shortPercentOfFloat;
    double  shortRatio;
    double  threeYearAverageReturn;
    int64_t totalAssets;
    double  trailingEps;
    double  yield;
    double  ytdReturn;
  } defaultKeyStatistics;

  /* struct Earnings */
  /* { */
  /*   struct EarningsChart */
  /*   { */
  /*     double  currentQuarterEstimate; */
  /*     YString currentQuarterEstimateDate; */
  /*     int64_t currentQuarterEstimateYear; */
  /*     int64_t earningsDate; */

#define QUARTERLY 4
  /*     struct Quarterly */
  /*     { */
  /*       double  actual; */
  /*       YString date; */
  /*       double  estimate; */
  /*     } quarterly[QUARTERLY]; */
  /*   } earningsChart; */

  struct FinancialsChart
  {
    union
    {
      int64_t int_date;
      YString str_date;
    } date;
    int64_t earnings;
    int64_t revenue;
  } /* financialsChartYearly[QUARTERLY], */ financialsChartQuarterly[QUARTERLY];

  /*   YString financialCurrency; */
  /* } earnings; */

  struct EarningsHistory
  {
    double  epsActual;
    double  epsDifference;
    double  epsEstimate;
    YString period;
    int64_t quarter;
    double  surprisePercent;
  } earningsHistory[QUARTERLY];

  struct EarningsTrend
  {
    YString endDate;
    double  growth;
    YString period;

    struct EarningsEstimate
    {
      double  avg;
      double  growth;
      double  high;
      double  low;
      int64_t numberOfAnalysts;
      double  yearAgoEps;
    } earningsEstimate;

    struct RevenueEstimate
    {
      int64_t avg;
      double  growth;
      int64_t high;
      int64_t low;
      int64_t numberOfAnalysts;
      int64_t yearAgoRevenue;
    } revenueEstimate;
  } earningsTrend[QUARTERLY];

  struct FinancialData
  {
    double  currentPrice;
    double  currentRatio;
    double  debtToEquity;
    double  earningsGrowth;
    int64_t ebitda;
    double  ebitdaMargins;
    YString financialCurrency;
    int64_t freeCashflow;
    double  grossMargins;
    int64_t grossProfits;
    int64_t numberOfAnalystOpinions;
    int64_t operatingCashflow;
    double  operatingMargins;
    double  profitMargins;
    double  quickRatio;
    YString recommendationKey;
    double  recommendationMean;
    double  returnOnAssets;
    double  returnOnEquity;
    double  revenueGrowth;
    double  revenuePerShare;
    double  targetHighPrice;
    double  targetLowPrice;
    double  targetMeanPrice;
    double  targetMedianPrice;
    int64_t totalCash;
    double  totalCashPerShare;
    int64_t totalDebt;
    int64_t totalRevenue;
  } financialData;
};

struct YChart
{
  /* struct Meta */
  /* { */
  double  chartPreviousClose;
  /*   enum TradingPeriod */
  /*   { */
  /*     PRE, REGULAR, POST, TRADING_PERIODS */
  /*   }; */
  /*   struct CurrentTradingPeriod */
  /*   { */
  /*     int64_t end; */
  /*     int64_t gmtoffset; */
  /*     int64_t start; */
  /*     YString timezone; */
  /*   } currentTradingPeriod[TRADING_PERIODS]; */
  /*   YString dataGranularity; */
  /*   YString range; */
  double  regularMarketPrice;
  YString symbol;
  /*   YString validRanges[0]; */
  /* } meta; */

  size_t  count;
  int64_t timestamp[YARRAY_LENGTH];

  /* struct Events */
  /* { */
  /*   struct Dividend */
  /*   { */
  /*     double  amount; */
  /*     int64_t date; */
  /*   } dividends[0]; */
  /* } events; */

  /* struct Indicators */
  /* { */
  /*   struct AdjClose */
  /*   { */
  double  adjclose [YARRAY_LENGTH];
  /*   } adjclose[1]; */

  /*   struct Quote */
  /*   { */
  double  close    [YARRAY_LENGTH];
  double  high     [YARRAY_LENGTH];
  double  low      [YARRAY_LENGTH];
  double  open     [YARRAY_LENGTH];
  int64_t volume   [YARRAY_LENGTH];
  /*   } quote[1]; */
  /* } indicators; */
};

struct YOption
{
  double  ask;
  double  bid;
  double  change;
  YString contractSize;
  YString contractSymbol;
  YString currency;
  int64_t expiration;
  double  impliedVolatility;
  bool    inTheMoney;
  double  lastPrice;
  int64_t lastTradeDate;
  int64_t openInterest;
  double  percentChange;
  double  strike;
  int64_t volume;
};

struct YOptionChain
{
/* #define EXPIRATION_DATES 24 */
  /* int64_t expirationDates[EXPIRATION_DATES]; */
  /* bool    hasMiniOptions; */
  size_t  count;
  double  strikes     [YARRAY_LENGTH];
  YString underlyingSymbol;

  /* struct Options */
  /* { */
  int64_t expirationDate;
  bool    hasMiniOptions;
  struct YOption calls[YARRAY_LENGTH];
  struct YOption puts [YARRAY_LENGTH];

  /*   struct Straddle */
  /*   { */
  /*     double  strike; */
  /*     struct YOption call; */
  /*     struct YOption put; */
  /*   } straddles[YARRAY_LENGTH]; */
  /* } options[1]; */
};

extern struct YError yql_error;

int  yql_init();
int  yql_open();
void yql_close();
void yql_free();

struct YQuote *yql_getQuote(const char *);
struct YQuoteSummary *yql_getQuoteSummary(const char *);
struct YChart *yql_getChart(const char *);
struct YOptionChain *yql_getOptionChain(const char *);

void yql_feQuote(void (*)(void *, void *, void *), void *);

int yql_quote(const char *);
int yql_quoteSummary(const char *);
int yql_earnings(const char *);
int yql_financials(const char *);
int yql_chart(const char *);
int yql_options(const char *);
int yql_download(const char *, int64_t);

#endif
