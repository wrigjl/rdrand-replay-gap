
gcc -O2 -o exp2_control exp2_intrinsic.c
objdump -d exp2_control | grep -E 'rdrand|cpuid'
rr record ./exp2_control 2> record_exp2_control.log
rr replay -a 2> replay_exp2_control.log
diff record_exp2_control.log replay_exp2_control.log
