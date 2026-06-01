
gcc -O2 -mrdrnd -o exp2_intrinsic exp2_intrinsic.c
objdump -d exp2_intrinsic | grep -E 'rdrand|cpuid'
rr record ./exp2_intrinsic 2> record_exp2_intrinsic.log
rr replay -a 2> replay_exp2_intrinsic.log
diff record_exp2_intrinsic.log replay_exp2_intrinsic.log
