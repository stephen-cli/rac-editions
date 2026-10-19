#!/bin/sh
# WSL side of EE_GAS210 / EE_DTLS29_AS on Windows (games/rac1's tools/gas210.py
# and tools/dtls29_as.py): run a Linux toolchain program with its arguments'
# backslashes turned into slashes. The launchers pass Windows paths
# (pathlib on Windows writes build-sn\core\X.o), which Linux would take as
# one file name in the current directory.
#
#   wsl -d Ubuntu -e sh /mnt/c/.../wsl_linux_run.sh PROGRAM [ARGS...]
prog=$1
shift
for a in "$@"; do
  set -- "$@" "$(printf '%s' "$a" | tr '\134' '/')"
  shift
done
exec "$prog" "$@"
