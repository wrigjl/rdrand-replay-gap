for f in \
  /lib/x86_64-linux-gnu/libgcrypt.so.20.1.6 \
  /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.22 \
  /usr/lib/x86_64-linux-gnu/libcrypto.so.1.1 \
  /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.2 \
; do
    mkdir -p `pwd`/`dirname $f`
    docker cp d2535799d943:$f `pwd`/$f
done
