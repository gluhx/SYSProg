savedcmd_ioctl_crypto_wallet.mod := printf '%s\n'   ioctl_crypto_wallet.o | awk '!x[$$0]++ { print("./"$$0) }' > ioctl_crypto_wallet.mod
