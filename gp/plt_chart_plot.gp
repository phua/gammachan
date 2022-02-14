# gnuplot> load "plt_chart_init.gp"
# gnuplot> $dat << EOD ... EOD
# gnuplot> call "plt_chart_plot.gp" "symbol" "timestamp"
#               "fiftyDayAverage" "twoHundredDayAverage" "targetMeanPrice"

symbol = ARG1
timestamp = ARG2 + 0
fiftyDayAverage = ARG3 + 0
twoHundredDayAverage = ARG4 + 0
targetMeanPrice = ARG5 + 0

set title sprintf("%s Chart [%s:]", symbol, strftime("%Y-%m-%d", timestamp))

plot \
     $dat using 1:7 axes x1y2 title "Volume" with impulses linecolor "gray" \
     , "" using 1:7 axes x1y2 notitle with points pointtype 7 linecolor "gray" \
     , targetMeanPrice axes x1y1 title "Target" linecolor "green" \
     , fiftyDayAverage axes x1y1 title "50-D MA" linecolor "yellow" \
     , twoHundredDayAverage axes x1y1 title "200-D MA" linecolor "orange" \
     , "" using 1:6 axes x1y1 title "Adj Close" with lines linecolor "pink" \
     , "" using 1:2:4:3:5 axes x1y1 notitle with candlesticks linecolor "purple"

# undefine $dat
