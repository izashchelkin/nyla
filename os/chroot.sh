ldconfig -r /nyla
env -i \
    HOME=/root \
    TERM="$TERM" \
    PS1='(nyla) \u:\w\$ ' \
    PATH=/usr/bin:/usr/sbin \
    chroot /nyla /usr/bin/bash --login
