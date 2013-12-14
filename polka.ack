#!/bin/sh
# This is the first real demo song I made for "ack". The name was inspired by
# Weird Al Yankovic's work. --jn

cat << __END
Cv3 Ct300
/0 Ws A5,10,.7,200 V55
/1 Wn A5,0,1,170  V30
/2 Wp A25,10,.8,60 V35
__END

BASS_LOOP=`mktemp`
cat << __END | tr '|' '\n' > $BASS_LOOP
/0 N62 TR || /0 N57 TR || /0 N62 TR || /0 N57 TR |
/0 N57 TR || /0 N52 TR || /0 N57 TR || /0 N52 TR |
/0 N59 TR || /0 N54 TR || /0 N59 TR || /0 N54 TR |
/0 N55 TR || /0 N50 TR || /0 N55 TR || /0 N50 TR |
__END

SNARE_LOOP=`mktemp`
(for i in `seq 0 15`; do echo; echo /1 TR; done) > $SNARE_LOOP

LEAD1=`mktemp`
cat << __END | tr '|' '\n' > $LEAD1
/2 N62T |	|	|
	|	|/2 N61T|/2N62T
/2 N61T |	|	|
	|	|	|
/2 N59T |	|	|
	|	|	| R
/2 T	|	|	|
	|	|	| R
__END

LEAD2=`mktemp`
cat << __END | tr '|' '\n' > $LEAD2
/2 N57T	|	|	|	
	|	|/2 N61T|/2 N62T
/2 N64T |	|	|
	|	|/2 N62T|/2 N61T
/2 N62T |	|	|
	|	|	|
/2 R	|	|	|
	|	|	|
__END

LEAD3=`mktemp`
cat << __END | tr '|' '\n' > $LEAD3
/2 N62T	|	|	|
	|	|/2 N61T| /2 N62T
/2 N64T |	|	|
	|	|/2 N62T|
/2 N66T |	|	|
	|	|/2 N64T|
/2 N67T |	|	|
	|	|	| /2 R
__END

LEAD4=`mktemp`
cat << __END | tr '|' '\n' > $LEAD4
/2 N69T	|	|	|
	|	|/2 N67T| /2 N66T
/2 N64T |	|	|
	|	|/2 N66T|
/2 N67T |/2 N62T|	|
	|	|/2 N64T|/2R
/2 N62T	|	|	|
	|	|	|/2 R
__END

ENDING=`mktemp`
for i in a b; do
	echo '/1 TR	|	||	||	||' | tr '|' '\n' >> $ENDING
	echo '		|/1 TR	||	||	||' | tr '|' '\n' >> $ENDING
done

TICK_LOOP=`mktemp`
(for i in `seq 0 31`; do echo //; done) > $TICK_LOOP

paste $BASS_LOOP $SNARE_LOOP $TICK_LOOP
paste $BASS_LOOP $SNARE_LOOP $LEAD1 $TICK_LOOP
paste $BASS_LOOP $SNARE_LOOP $LEAD2 $TICK_LOOP
paste $BASS_LOOP $SNARE_LOOP $LEAD3 $TICK_LOOP
paste $BASS_LOOP $SNARE_LOOP $LEAD4 $TICK_LOOP
paste $BASS_LOOP $SNARE_LOOP $TICK_LOOP
paste $BASS_LOOP $ENDING $TICK_LOOP
echo /1 TR // /0 N62 T // // // // // // R //
echo /q

rm $BASS_LOOP $SNARE_LOOP $TICK_LOOP
rm $LEAD1 $LEAD2 $LEAD3 $LEAD4 $ENDING
