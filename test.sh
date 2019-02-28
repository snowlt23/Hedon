#!/bin/sh

runtest() {
  OUT=$(echo "$1" | ./hedon)
  echo "[TEST] $1"
  if [ "$OUT" != "$2" ] ; then
    echo "[ERROR] $1: expect $2, but got $OUT "
    exit 1
  fi
}

runtest "4 5 +" "9"
runtest "6 5 -" "1"
runtest "0 1 -" "-1"
