#pragma once
#ifndef SAMPLE_STATISTICS_H
#define SAMPLE_STATISTICS_H

#include <gsl/gsl_matrix.h>

#include "../include/yql.h"

struct SummaryStatistics
{
  size_t nsize;

  double minimum;
  double lowerQuartile;
  double median;
  double upperQuartile;
  double maximum;

  double mean;
  double variance;
  double stddev;
  double skewness;
  double kurtosis;

  double covariance;
  double correlation;
  double slope;
  double intercept;
};

double sum(const double *, size_t);
double mean(const double *, size_t);
double variance(const double *, size_t);
double stddev(const double *, size_t);

double covariance(const double *, const double *, size_t);
double correlation(const double *, const double *, size_t);

void summary(struct SummaryStatistics *, const double *, size_t);

struct Bollinger
{
  size_t N;
  double K;

  size_t   size;
  int64_t *ts;
  double  *ma;
  double  *sd;
};

void Bollinger_init(struct Bollinger *, size_t);
void Bollinger_free(struct Bollinger *);
void bollinger(struct Bollinger *, const int64_t *, const double *, size_t);
void bollinger_c(struct Bollinger *, const struct YChart * const);

typedef struct datable_t
{
  size_t rank;
  size_t size;
  int64_t    *timestamp;
  gsl_matrix *data;
  gsl_matrix *rmat;
  struct SummaryStatistics *bvec;

  const struct YChart **charts;
} datable_t;

void datable_init(datable_t *, size_t, const struct YChart *[]);
void datable_free(datable_t *);
void datable_fit(datable_t *);

#endif
