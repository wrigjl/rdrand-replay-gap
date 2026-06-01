echo 'int main(){}' | gcc -march=native -dM -E -x c - | grep RDRND
gcc -O2 -march=native -o exp2_native exp2_intrinsic.c
echo -march=native
objdump -d exp2_native | grep -E 'rdrand|cpuid'
gcc -O2 -o exp2_stock exp2_intrinsic.c
echo -march=stock
objdump -d exp2_stock | grep -E 'rdrand|cpuid'
