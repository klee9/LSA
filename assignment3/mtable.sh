#!/bin/sh
# handle exceptions
if [ -z $2 ]; then
	echo "Invalid input"
	exit
fi

if [ $1 -lt 0 ] || [ $2 -lt 0 ]; then
	echo "Input must be greater than 0"
	exit
fi

# print multiplication table
for i in $(seq 1 `expr $1`); do
	for j in $(seq 1 `expr $2`); do
		printf "%d*%d=%d\t" $i $j `expr $i \* $j`
	done
	printf "\n"
done

exit 0
