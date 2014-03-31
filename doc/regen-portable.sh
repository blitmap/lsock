#/bin/sh

# make sure these are sorted
for x in ./*-constants.txt
do
	sort -u $x >> $x'2'
	mv $x'2' $x
done

comm -1 -2    mac-constants.txt linux-constants.txt >   mac-linux-shared.txt
comm -1 -2 mac-linux-shared.txt   win-constants.txt > portable-constants.txt

rm mac-linux-shared.txt
