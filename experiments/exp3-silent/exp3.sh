gcc -O2 -mrdrnd -o exp3_silent exp3_silent.c
rr record ./exp3_silent > record_output.bin 2>record.log
rr replay -a > replay_output.bin 2>replay.log
echo "Exit codes match: $(diff <(echo $?) <(echo 0))"
echo "rr reported no errors: $(grep -c error replay.log)"
echo "Output differs:"
diff record_output.bin replay_output.bin

