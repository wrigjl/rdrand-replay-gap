for f in \
/usr/bin/x86_64-linux-gnu-ld.gold \
/usr/bin/x86_64-linux-gnu-dwp \
/usr/bin/x86_64-linux-gnu-gcov-12 \
/usr/bin/x86_64-linux-gnu-lto-dump-12 \
/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.30 \
/usr/lib/x86_64-linux-gnu/libgcrypt.so.20.4.1 \
/usr/lib/x86_64-linux-gnu/libsodium.so.23.3.0 \
/usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
/usr/lib/x86_64-linux-gnu/ossl-modules/legacy.so \
/usr/lib/gcc/x86_64-linux-gnu/12/cc1 \
/usr/lib/gcc/x86_64-linux-gnu/12/cc1plus \
/usr/lib/gcc/x86_64-linux-gnu/12/lto1 \
/usr/lib/gcc/x86_64-linux-gnu/12/g++-mapper-server \
/usr/sbin/apparmor_parser \
; do
    mkdir -p `pwd`/`dirname $f`
    docker cp 5e14b83a5496:$f `pwd`/$f
done
