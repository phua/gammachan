# gnuplot> array S[size] ...
# gnuplot> $dat << EOD ... EOD
# gnuplot> $bol << EOD ... EOD
# gnuplot> $map << EOD ... EOD
# gnuplot> call "./gp/plt_chart_basket.gp" "startDate" "endDate" "size"

startDate = ARG1 + 0
endDate = ARG2 + 0
size = ARG3 + 0

load "./gp/plt_chart_init.gp"

set multiplot

# Plot Chart

set origin 0.00, 0.50
set size 1.00, 0.50

set title sprintf("%s Chart [%s:%s]", S[1], strftime("%Y-%m-%d", startDate), strftime("%Y-%m-%d", endDate))

plot \
     $bol using 1:($2 + $3):($2 - $3) axes x1y1 title "Bollinger Band" with filledcurves linecolor rgb "0xEBF1F3" \
     , "" using 1:2 axes x1y1 title "10-D SMA" with lines linecolor rgb "0xCCDBE1" \
     , $dat1 using 1:7 axes x1y2 title "Volume" with impulses linecolor "gray" \
     , "" using 1:7 axes x1y2 notitle with points pointtype 7 linecolor "gray" \
     , "" using 1:6 axes x1y1 title "Adj Close" with lines linecolor "pink" \
     , "" using 1:2:4:3:5 axes x1y1 notitle with candlesticks linecolor "purple"

# Plot Holdings

set origin 0.00, 0.00
set size 0.50, 0.50

set ytics mirror
unset y2tics

set title sprintf("%s Top Holdings", S[1])
unset y2label

plot \
     for [i=2:size] "$dat".i using 1:6 axes x1y1 title S[i] with linespoints \

# Plot Correlation Matrix

set origin 0.50, 0.00
set size 0.50, 0.50

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
     , "" matrix rowheaders columnheaders using 1:2:(sprintf("%g", $3)) with labels

unset multiplot

undefine startDate endDate size
undefine S
# undefine $dat*
# undefine $bol
# undefine $map
undefine $*

quit
