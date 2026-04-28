savedcmd_proc_crypto_wallet.mod := printf '%s\n'   proc_crypto_wallet.o | awk '!x[$$0]++ { print("./"$$0) }' > proc_crypto_wallet.mod
