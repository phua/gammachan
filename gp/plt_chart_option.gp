# gnuplot> $dat << EOD ... EOD
# gnuplot> $und << EOD ... EOD
# gnuplot> call "./gp/plt_chart_option.gp" "symbol" "startDate" "endDate" "underlyingSymbol" "strike"

symbol = ARG1
startDate = ARG2 + 0
endDate = ARG3 + 0
underlyingSymbol = ARG4
strike = ARG5 + 0

load "./gp/plt_chart_init.gp"

set datafile missing "0.000000"

set multiplot

# Plot Price

set origin 0.00, 0.25
set size 1.00, 0.75

set title sprintf("%s v. %s Chart [%s:%s]", symbol, underlyingSymbol, strftime("%Y-%m-%d", startDate), strftime("%Y-%m-%d", endDate))
set ylabel sprintf("%s Price", symbol)
set y2label sprintf("%s Price", underlyingSymbol)

set link y2 via y + strike inverse y - strike

# plot [] [0:*] ...
plot \
     $und using 1:6 axes x1y2 title underlyingSymbol with lines \
     , $dat using 1:6 axes x1y1 title symbol with lines

unset link y2

# Plot Volume

set origin 0.00, 0.00
set size 1.00, 0.25

unset title
unset xlabel
unset ylabel
unset y2label

set style fill solid 1.0 border linetype -1

plot \
     $und using 1:7 axes x1y2 title underlyingSymbol with boxes \
     , $dat using 1:7 axes x1y1 title symbol with boxes

unset multiplot

undefine symbol startDate endDate underlyingSymbol strike
undefine $dat
undefine $und

quit
