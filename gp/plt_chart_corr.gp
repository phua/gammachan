# gnuplot> array S[size] Br[size] B0[size] B1[size] ...
# gnuplot> $dat << EOD ... EOD
# gnuplot> $tab << EOD ... EOD
# gnuplot> $map << EOD ... EOD
# gnuplot> call "./gp/plt_chart_corr.gp" "startDate" "endDate" "size"

startDate = ARG1 + 0
endDate = ARG2 + 0
size = ARG3 + 0

load "./gp/plt_chart_init.gp"

set multiplot

# Plot Chart

set origin 0.00, 0.50
set size 1.00, 0.50

set ytics mirror
unset y2tics

set title sprintf("%s v. ... Chart [%s:%s]", S[1], strftime("%Y-%m-%d", startDate), strftime("%Y-%m-%d", endDate))
unset y2label

plot \
     for [i=1:size] "$dat".i using 1:6 axes x1y1 title S[i] with linespoints pointtype i linecolor i

# Plot Correlation

set origin 0.00, 0.00
set size 0.75, 0.50

set xdata
set xtics format "%h"

set title sprintf("%s v. ... Correlation Plot [%s:%s]", S[1], strftime("%Y-%m-%d", startDate), strftime("%Y-%m-%d", endDate))
set xlabel sprintf("%s Price", S[1])
set ylabel "... Price"

# f(x) = b0 + b1 * x
# fit f(x) $tab using 2:3 via b0, b1

# plot "< join -j 1 -t , -" ...
plot \
     for [i=2:size] $tab using 2:i+1 axes x1y1 notitle with points pointtype i linecolor i \
     , for [i=2:size] B0[i] + B1[i] * x title sprintf("%s = %.2f + %.2f * x, r = %.2f", S[i], B0[i], B1[i], Br[i]) linecolor i
# , f(x) title sprintf("f(x) = %.2f + %.2f * x", b0, b1)

# Plot Correlation Matrix

set origin 0.75, 0.00
set size 0.25, 0.50

set title "Correlation Heat Map"
unset xlabel
unset ylabel
unset y2label
unset key

set cblabel "r Coefficient"
# set cbrange [-1:1]
# set palette rgbformula -7,2,-7

plot \
     $map matrix rowheaders columnheaders using 1:2:3 with image \
     , "" matrix rowheaders columnheaders using 1:2:(sprintf("%.3g", $3)) with labels

unset multiplot

undefine startDate endDate size
undefine S B*
# undefine $dat*
# undefine $tab
# undefine $map
undefine $*

quit
