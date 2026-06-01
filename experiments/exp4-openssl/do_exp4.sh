g++ -O2 -o exp4_stdrand exp4_stdrand.cpp
rr record ./exp4_stdrand > rec.txt 2>rec.log
echo "record exit: $?"
rr replay -a > rep.txt 2>rep.log
echo "replay exit: $?"
diff rec.txt rep.txt
echo "diff exit: $?"
cat rec.log rep.log   # should be clean
