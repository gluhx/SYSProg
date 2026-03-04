savedcmd_kernel_hello.mod := printf '%s\n'   kernel_hello.o | awk '!x[$$0]++ { print("./"$$0) }' > kernel_hello.mod
