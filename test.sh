#!/bin/sh

basetest() {
  OUT=$(echo "$1" | ./hedon)
  echo "[TEST] $1"
  if [ "$OUT" != "$2" ] ; then
    echo "[ERROR] $1    expect $2, but got $OUT "
    exit 1
  fi
}

runtest() {
  OUT=$(echo "$(cat prelude.hedon) $1" | ./hedon)
  echo "[TEST] $1"
  if [ "$OUT" != "$2" ] ; then
    echo "[ERROR] $1    expect $2, but got $OUT"
    exit 1
  fi
}

filetest() {
  OUT=$(echo "$(cat prelude.hedon) $(cat $1)" | ./hedon)
  echo "[TEST] $1"
  if [ "$OUT" != "$2" ] ; then
    echo "[ERROR] $1    expect $2, but got $OUT"
    exit 1
  fi
}

errortest() {
  ERR=$(echo "$(cat prelude.hedon) $1" | ./hedon 2>&1)
  echo "[TEST] $1"
  if [ "$ERR" != "$2" ] ; then
    echo "[ERROR] $1    expect \"$2\" error, but got \"$ERR\""
    exit 1
  fi
}

error_filetest() {
  ERR=$(echo "$(cat prelude.hedon) $(cat $1)" | ./hedon 2>&1)
  echo "[TEST] $1"
  if [ "$ERR" != "$2" ] ; then
    echo "[ERROR] $1    expect \"$2\" error, but got \"$ERR\""
    exit 1
  fi
}

basetest "555" "555"
basetest "4 5 +" "9"
basetest "6 5 -" "1"
basetest "0 1 -" "-1"
basetest ": add5 5 + ; 4 add5" "9"
basetest ": drop X 0x48 X 0x83 X 0xc3 X 0x08 ; 4 5 drop" "4"
basetest ": add5 5 + ; dump-label add5 0" "Int -- Int0"
basetest ": add5 5 + ; : x9 4 add5 ; x9" "9"

runtest "4 5 drop" "4"
runtest "555 . cr 0" "555
0"
errortest ": main ; drop" "stack is empty, but expected Int value in main"
errortest ": add5 5 + ; 5 ( drop )" "stack is empty, but expected Label value in add5"
error_filetest "examples/basic-type.hedon" "unmatch MyInt label to Int in mi"
filetest "examples/union-type.hedon" "18"
