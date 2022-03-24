/* #include <assert.h> */
#include <math.h>
#include <string.h>

#include <gsl/gsl_statistics.h>

#include "../include/log.h"
#include "../include/stats.h"
#include "../include/util.h"

/* #define EPSILON 1.0e-7 */

double sum(const double *data, size_t size)
{
  double sum = 0.0;

  for (size_t i = 0; i < size; i++) {
    sum += data[i];
  }

  return sum;
}

double mean(const double *data, size_t size)
{
  return size > 0 ? sum(data, size) / size : 0.0;
}

/*
 * Welford's algorithm
 */
double variance(const double *data, size_t size)
{
  if (size < 2) {
    return 0.0;
  }

  double M1_prev = *data, M1_next = *data, M2_prev = 0.0, M2_next = 0.0;

  for (size_t i = 1; i < size; i++) {
    M1_next = M1_prev + (data[i] - M1_prev) / (i + 1);
    M2_next = M2_prev + (data[i] - M1_prev) * (data[i] - M1_next);
    M1_prev = M1_next, M2_prev = M2_next;
  }

  /* assert(fabs(M1_next                    - gsl_stats_mean(data, 1, size)) < EPSILON); */
  /* assert(fabs(M2_next / (size - 1)       - gsl_stats_variance(data, 1, size)) < EPSILON); */
  /* assert(fabs(sqrt(M2_next / (size - 1)) - gsl_stats_sd(data, 1, size)) < EPSILON); */

  return M2_next / (size - 1);
}

double stddev(const double *data, size_t size)
{
  return sqrt(variance(data, size));
}

double covariance(const double *data1, const double *data2, size_t size)
{
  if (size < 2) {
    return 0.0;
  }

  double M_x = 0.0, M_y = 0.0, C = 0.0;

  for (size_t i = 0; i < size; i++) {
    double dx = data1[i] - M_x;
    M_x += dx / (i + 1);
    M_y += (data2[i] - M_y) / (i + 1);
    C += dx * (data2[i] - M_y);
  }

  /* assert(fabs(C / (size - 1) - gsl_stats_covariance(data1, 1, data2, 1, size)) < EPSILON); */

  return C / (size - 1);
}

double correlation(const double *data1, const double *data2, size_t size)
{
  if (size < 2) {
    return 0.0;
  }

  double C = covariance(data1, data2, size) / (stddev(data1, size) * stddev(data2, size));

  /* assert(fabs(C - gsl_stats_correlation(data1, 1, data2, 1, size)) < EPSILON); */

  return C;
}

/*
 * https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
 */
void summary(struct SummaryStatistics *s, const double *data, size_t size)
{
  double M1 = 0.0, M2 = 0.0, M3 = 0.0, M4 = 0.0;

  for (size_t i = 0; i < size; i++) {
    int n = i + 1;
    double delta = data[i] - M1;
    double delta_n = delta / n;
    double delta_n2 = delta_n * delta_n;
    double term1 = delta * delta_n * (n - 1);
    M1 += delta_n;
    M4 += term1 * delta_n2 * (n * n - 3 * n + 3) + 6 * delta_n2 * M2 - 4 * delta_n * M3;
    M3 += term1 * delta_n * (n - 2) - 3 * delta_n * M2;
    M2 += term1;
  }

  s->nsize = size;
  s->mean = M1;
  s->variance = size > 1 ? M2 / (size - 1) : 0.0;
  s->stddev = sqrt(s->variance);
  s->skewness = M2 > 0.0 ? sqrt(size) * M3 / pow(M2, 1.5) : 0.0;
  s->kurtosis = M2 > 0.0 ? (size * M4) / (M2 * M2) - 3 : 0.0;

  /* assert(fabs(s->mean     - gsl_stats_mean(data, 1, size)) < EPSILON); */
  /* assert(fabs(s->variance - gsl_stats_variance(data, 1, size)) < EPSILON); */
  /* assert(fabs(s->stddev   - gsl_stats_sd(data, 1, size)) < EPSILON); */
  /* assert(fabs(s->skewness - gsl_stats_skew(data, 1, size)) < EPSILON); */
  /* assert(fabs(s->kurtosis - gsl_stats_kurtosis(data, 1, size)) < EPSILON); */
}

void summary_gsl(struct SummaryStatistics *s, const double *data, size_t stride, size_t size)
{
  s->nsize = size;
  s->mean = gsl_stats_mean(data, stride, size);
  s->variance = gsl_stats_variance(data, stride, size);
  s->stddev = gsl_stats_sd(data, stride, size);
  s->skewness = gsl_stats_skew(data, stride, size);
  s->kurtosis = gsl_stats_kurtosis(data, stride, size);
}

struct Bollinger *Bollinger_new(size_t N, double K, size_t size)
{
  struct Bollinger *b = malloc(sizeof(struct Bollinger));
  b->N = N;
  b->K = K;
  Bollinger_init(b, size);
  return b;
}

void Bollinger_init(struct Bollinger *b, size_t size)
{
  b->size = size;
  b->ts = calloc(b->size, sizeof(int64_t));
  b->ma = calloc(b->size, sizeof(double));
  b->sd = calloc(b->size, sizeof(double));
  if (!b->ts || !b->ma || !b->sd) {
    log_default("%s:%d: calloc(%zu): %s\n", __FILE__, __LINE__, b->size, strerror(errno));
    Bollinger_free(b);
    return;
  }
}

void Bollinger_free(struct Bollinger *b)
{
  b->size = 0;
  if (b->ts) {
    free(b->ts); b->ts = NULL;
  }
  if (b->ma) {
    free(b->ma); b->ma = NULL;
  }
  if (b->sd) {
    free(b->sd); b->sd = NULL;
  }
}

void Bollinger_delete(struct Bollinger *b)
{
  if (b) {
    Bollinger_free(b);
    free(b);
  }
}

void bollinger(struct Bollinger *b, const int64_t *ts, const double *data, size_t size)
{
  if (size < b->N) {
    return;
  }
  Bollinger_init(b, size - b->N + 1);
  /* b->ts = ts + (b->N - 1); */

  double x = 0.0, sum = 0.0, tss = 0.0;

  for (size_t i = 0, j = 0; i < size && j < b->size; i++) {
    x = data[i], sum += x, tss += x * x;
    if (i >= b->N - 1) {        /* j = i - (b->N - 1); */
      b->ts[j] = ts[i];
      b->ma[j] = sum / b->N;
      b->sd[j] = sqrt((tss - 2 * b->ma[j] * sum + b->N * (b->ma[j] * b->ma[j])) / (b->N - 1));

      /* assert(fabs(b->ma[j] - gsl_stats_mean(data + j, 1, b->N)) < EPSILON); */
      /* assert(fabs(b->sd[j] - gsl_stats_sd(data + j, 1, b->N)) < EPSILON); */

      x = data[i - (b->N - 1)], sum -= x, tss -= x * x;
      j++;
    }
  }
}

void bollinger_c(struct Bollinger *b, const struct YChart * const c)
{
  bollinger(b, c->timestamp, c->adjclose, c->count);
}

void datable_init(datable_t *t, size_t rank, const struct YChart *c[])
{
  t->rank = rank;
  t->size = 0;
  t->timestamp = NULL;
  t->data = gsl_matrix_calloc(t->rank, YARRAY_LENGTH);
  t->rmat = gsl_matrix_calloc(t->rank, t->rank);
  t->bvec = calloc(t->rank, sizeof(struct SummaryStatistics));
  t->charts = arrcpy(struct YChart *, c, rank);
  if (!t->bvec || !t->charts) {
    log_default("%s:%d: calloc(%zu): %s\n", __FILE__, __LINE__, t->rank, strerror(errno));
    datable_free(t);
    return;
  }
}

void datable_free(datable_t *t)
{
  t->size = 0;
  if (t->timestamp) {
    free(t->timestamp);       t->timestamp = NULL;
  }
  if (t->data) {
    gsl_matrix_free(t->data); t->data = NULL;
  }
  if (t->rmat) {
    gsl_matrix_free(t->rmat); t->rmat = NULL;
  }
  if (t->bvec) {
    free(t->bvec);            t->bvec = NULL;
  }
  if (t->charts) {
    free(t->charts);          t->charts = NULL;
  }
}

static size_t intersect(const int64_t *A, size_t m, const int64_t *B, size_t n, int64_t **C, size_t *o)
{
  *C = calloc(m, sizeof(int64_t)), *o = 0;

  for (size_t i = 0, j = 0; i < m && j < n; ) {
    int64_t x = gmbod(A[i]);
    int cmp = x - gmbod(B[j]);
    if (cmp < 0) {
      i++;
    } else if (cmp > 0) {
      j++;
    } else {
      *(*C + (*o)++) = x;
      i++, j++;
    }
  }

  return *o;
}

static size_t datable_intersect(datable_t *t)
{
  const struct YChart *c = t->charts[0];
  t->timestamp = arrcpy(int64_t, c->timestamp, c->count), t->size = c->count;

  for (size_t i = 1, n = 1; i < t->rank && n > 0; i++) {
    c = t->charts[i];
    int64_t *S = NULL;
    intersect(t->timestamp, t->size, c->timestamp, c->count, &S, &n);
    free(t->timestamp);
    t->timestamp = S, t->size = n;
  }

  return t->size;
}

static void paste(datable_t *t, size_t r, const struct YChart * const c)
{
  for (size_t i = 0, j = 0; i < c->count && j < t->size; i++) {
    if (gmbod(c->timestamp[i]) == t->timestamp[j]) {
      gsl_matrix_set(t->data, r, j++, c->adjclose[i]);
    }
  }
}

static void datable_paste(datable_t *t)
{
  for (size_t i = 0; i < t->rank; i++) {
    paste(t, i, t->charts[i]);
  }
}

static size_t datable_join(datable_t *t)
{
  if (datable_intersect(t) > 0) {
    datable_paste(t);
  }
  return t->size;
}

static void datable_runstat(datable_t *t)
{
  for (size_t i = 0; i < t->rank; i++) {
    gsl_vector_const_view vi = gsl_matrix_const_subrow(t->data, i, 0, t->size);
    summary_gsl(&t->bvec[i], vi.vector.data, vi.vector.stride, t->size);
    for (size_t j = 0; j < i + 1; j++) {
      gsl_vector_const_view vj = gsl_matrix_const_subrow(t->data, j, 0, t->size);
      double r = gsl_stats_correlation(vi.vector.data, vi.vector.stride, vj.vector.data, vj.vector.stride, t->size);
      gsl_matrix_set(t->rmat, i, j, r);
      gsl_matrix_set(t->rmat, j, i, r);
    }
    if (i > 0) {
      t->bvec[i].correlation = gsl_matrix_get(t->rmat, 0, i);
      t->bvec[i].slope = t->bvec[i].correlation * (t->bvec[i].stddev / t->bvec[0].stddev);
      t->bvec[i].intercept = t->bvec[i].mean - t->bvec[i].slope * t->bvec[0].mean;
    }
  }
}

void datable_fit(datable_t *t)
{
  if (datable_join(t) > 0) {
    datable_runstat(t);
  }
}
