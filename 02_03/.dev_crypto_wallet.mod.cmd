savedcmd_dev_crypto_wallet.mod := printf '%s\n'   dev_crypto_wallet.o | awk '!x[$$0]++ { print("./"$$0) }' > dev_crypto_wallet.mod
