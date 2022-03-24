# gnuplot> $dat << EOD ... EOD
# gnuplot> call "./gp/plt_chart_cppi.gp"

load "./gp/plt_chart_init.gp"

set ytics mirror
unset y2tics

set title "Consumer Price Index v. Producer Price Index"
set ylabel "Value"
unset y2label

title0 = "All items in U.S. city average, all urban consumers, not seasonally adjusted"
title1 = "All items in U.S. city average, urban wage earners and clerical workers, not seasonally adjusted"
title2 = "PPI Commodity data for All commodities, not seasonally adjusted"

plot ["1997-01-01":] \
     $dat index 0 using 1:2 title title0 with points \
     , "" index 1 using 1:2 title title1 with points \
     , "" index 2 using 1:2 title title2 with points

undefine $dat

quit
