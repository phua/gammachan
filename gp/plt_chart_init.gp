reset

# set terminal dumb
# set terminal qt size 1600,900

set datafile separator ","
set datafile missing "0.000000"

set xdata time
set timefmt "%Y-%m-%d"
set xtics format "%Y-%m-%d" # rotate
set ytics nomirror
set y2tics

set title "Gammaplot"
set xlabel "Date"
set ylabel "Price"
set y2label "Volume"
set key left opaque box
set grid
