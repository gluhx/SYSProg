savedcmd_sys_crypto_wallet.mod := printf '%s\n'   sys_crypto_wallet.o | awk '!x[$$0]++ { print("./"$$0) }' > sys_crypto_wallet.mod
