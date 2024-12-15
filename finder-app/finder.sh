#!/bin/sh

if [ "$#" -ne 2 ]; then
echo "Please provide input 'filesdir' and 'searchstr'"
exit 1
fi

if [ ! -d $1 ]; then
echo "Error: 'filsdir' does not represent a directory in filesystem"
exit 1
fi

filesdir=$1
searchstr=$2

matchcount=$(grep -R -c ${searchstr} ${filesdir} | awk -F: '{sum += $2} END {print sum}')

filecount=$(grep -rl  ${searchstr} ${filesdir} | wc -l)

echo "The number of files are ${filecount} and the number of matching lines are ${matchcount}"
