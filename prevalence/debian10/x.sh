for f in \
 /bin/systemd-hwdb \
 /bin/systemctl \
 /bin/udevadm \
 /lib/udev/scsi_id \
 /lib/udev/cdrom_id \
 /lib/x86_64-linux-gnu/security/pam_systemd.so \
 /lib/x86_64-linux-gnu/libsystemd.so.0.25.0 \
 /lib/x86_64-linux-gnu/libudev.so.1.6.13 \
 /lib/x86_64-linux-gnu/libgcrypt.so.20.2.4 \
 /lib/x86_64-linux-gnu/libnss_systemd.so.2 \
 /lib/x86_64-linux-gnu/libnss_myhostname.so.2 \
 /lib/systemd/systemd-networkd \
 /lib/systemd/systemd-udevd \
 /lib/systemd/libsystemd-shared-241.so \
 /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.25 \
 /usr/lib/x86_64-linux-gnu/libcrypto.so.1.1 \
 /usr/lib/x86_64-linux-gnu/libsodium.so.23.2.0 \
 /usr/lib/NetworkManager/nm-iface-helper \
 /usr/sbin/NetworkManager \
; do
    mkdir -p `pwd`/`dirname $f`
    docker cp 2e6ce070f0d5:$f `pwd`/$f
done
