# gnuplot> $dat << EOD ... EOD
# gnuplot> $bol << EOD ... EOD
# gnuplot> call "./gp/plt_chart_plot.gp" "symbol" "startDate" "endDate"
#               "fiftyDayAverage" "twoHundredDayAverage" "targetMeanPrice"

symbol = ARG1
startDate = ARG2 + 0
endDate = ARG3 + 0
fiftyDayAverage = ARG4 + 0
twoHundredDayAverage = ARG5 + 0
targetMeanPrice = ARG6 + 0

load "./gp/plt_chart_init.gp"

set title sprintf("%s Chart [%s:%s]", symbol, strftime("%Y-%m-%d", startDate), strftime("%Y-%m-%d", endDate))

# set logscale y 2

plot \
     $bol using 1:($2 + $3):($2 - $3) axes x1y1 title "Bollinger Band" with filledcurves linecolor rgb "0xEBF1F3" \
     , "" using 1:2 axes x1y1 title "10-D SMA" with lines linecolor rgb "0xCCDBE1" \
     , $dat using 1:7 axes x1y2 title "Volume" with impulses linecolor "gray" \
     , "" using 1:7 axes x1y2 notitle with points pointtype 7 linecolor "gray" \
     , targetMeanPrice axes x1y1 title "Target" linecolor "green" \
     , fiftyDayAverage axes x1y1 title "50-D Avg" linecolor "yellow" \
     , twoHundredDayAverage axes x1y1 title "200-D Avg" linecolor "orange" \
     , "" using 1:6 axes x1y1 title "Adj Close" with lines linecolor "pink" \
     , "" using 1:2:4:3:5 axes x1y1 notitle with candlesticks linecolor "purple"

undefine symbol startDate endDate fiftyDayAverage twoHundredDayAverage targetMeanPrice
undefine $dat
undefine $bol

quit
