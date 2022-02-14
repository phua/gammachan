# gnuplot> load "plt_chart_init.gp"
# gnuplot> $dat << EOD ... EOD
# gnuplot> call "plt_chart_hist.gp" "symbol" "startDate" "endDate"

symbol = ARG1
startDate = ARG2 + 0
endDate = ARG3 + 0

set title sprintf("%s Historical Prices [%s:%s]", symbol, strftime("%Y-%m-%d", startDate), strftime("%Y-%m-%d", endDate))

set logscale y 2

plot \
     $dat using 1:7 axes x1y2 title "Volume" with impulses linecolor "gray" \
     , "" using 1:7 axes x1y2 notitle with points pointtype 7 linecolor "gray" \
     , "" using 1:6 axes x1y1 title "Adj Close" with lines linecolor "pink" \
     , "" using 1:2:4:3:5 axes x1y1 notitle with financebars linecolor "purple"

# undefine $dat
