#!/bin/bash

# NOTE: The script works in context of current working directory.

[ -z "${DATABASE}" ] && DATABASE="sha1"

if ! ls *.bin 1> /dev/null 2>&1; then
  echo "No database files found. Downloading..."
  [ "${DATABASE}" = "sha1" ] && hibp-download hibp_all.sha1.bin
  [ "${DATABASE}" = "sha1t64" ] && hibp-download --sha1t64 hibp_all.sha1t64.bin
  [ "${DATABASE}" = "ntlm" ] && hibp-download --ntlm hibp_all.ntlm.bin
  [ "${DATABASE}" = "binfuse16" ] && hibp-download --binfuse16-out hibp_binfuse16.bin
  [ "${DATABASE}" = "binfuse8" ] && hibp-download --binfuse8-out hibp_binfuse8.bin
fi

[ -z "${EXTRA_ARGS}" ] && EXTRA_ARGS="--toc"

ARGS=""

[ -f "hibp_all.sha1.bin" ] && echo "Using database sha1" && ARGS="$ARGS --sha1-db=hibp_all.sha1.bin"
[ -f "hibp_all.sha1t64.bin" ] && echo "Using database sha1t64" && ARGS="$ARGS --sha1t64-db=hibp_all.sha1t64.bin"
[ -f "hibp_all.ntlm.bin" ] && echo "Using database ntlm" && ARGS="$ARGS --ntlm-db=hibp_all.ntlm.bin"
[ -f "hibp_binfuse16.bin" ] && echo "Using database binfuse16" && ARGS="$ARGS --binfuse16-filter=hibp_binfuse16.bin"
[ -f "hibp_binfuse8.bin" ] && echo "Using database binfuse8" && ARGS="$ARGS --binfuse8-filter=hibp_binfuse8.bin"

if [ -z "$ARGS" ]; then
  echo "No database files found. Exiting."
  exit 1
fi

# Function to process TERM signal
_term() {
  echo "Caught SIGTERM signal, stopping"
  kill -TERM "$pid" 2>/dev/null
}

# Capture TERM signal and execute function
trap _term SIGTERM

hibp-server --bind-address 0.0.0.0 ${ARGS} ${EXTRA_ARGS} &

# Store PID of celery worker
pid=$!
wait "$pid"
