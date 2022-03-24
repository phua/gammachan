#pragma once
#ifndef BLS_LABSTAT_H
#define BLS_LABSTAT_H

#include <stdint.h>

#define BLS_LABSTAT_SERIES "https://download.bls.gov/pub/time.series/"
#define BLS_LABSTAT_CPI_U  BLS_LABSTAT_SERIES "cu/cu.data.1.AllItems"
#define BLS_LABSTAT_CPI_W  BLS_LABSTAT_SERIES "cw/cw.data.1.AllItems"
#define BLS_LABSTAT_PPI    BLS_LABSTAT_SERIES "wp/wp.data.1.AllCommodities"
#define BLS_API_SERIES     "https://api.bls.gov/publicAPI/v1/timeseries/data/"

#define BLS_SERIES_ID_LENGTH 17
#define BLS_YEAR_LENGTH      4
#define BLS_PERIOD_LENGTH    3
#define BLS_VALUE_LENGTH     12
#define BLS_VALUE_PRECISION  3
#define BLS_FOOTNOTES_LENGTH 10
#define BLS_DATE_LENGTH      10

#define BLS_SERIES_ID_CPI_U "CUUR0000SA0"
#define BLS_SERIES_ID_CPI_W "CWUR0000SA0"
#define BLS_SERIES_ID_PPI   "WPU00000000"
#define BLS_PERIOD_MONTHLY  'M'
#define BLS_PERIOD_AN_AV    "M13"

#define BLS_RECORD_DELIM "\r\n"
#define BLS_FIELD_DELIM  " \t"
#define BLS_DATE_NULL    "0000-00-00"

typedef char BLS_SERIES_ID[BLS_SERIES_ID_LENGTH + 1];

struct BLSData
{
  BLS_SERIES_ID series;
  int16_t year;
  char    period[BLS_PERIOD_LENGTH + 1];
  double  value;
  char    date[BLS_DATE_LENGTH + 1];
};

typedef void (*bls_data_handler)(void *, const struct BLSData *);

void bls_download(bls_data_handler, void *);

#endif
