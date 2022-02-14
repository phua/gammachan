#pragma once
#ifndef PLOT_H
#define PLOT_H

#include <stdio.h>

#include "../include/yql.h"

/* #define PLT_EOT 0x04 */
/* #define PLT_SUB 0x1A */

typedef FILE *Plot;

Plot plt_gpopen();
void plt_gpclose(Plot);

void plt_gpplot_chart(Plot, const struct YChart * const);
void plt_gpplot_hist(Plot, const char *, int64_t, int64_t, const char *, size_t);

#endif
