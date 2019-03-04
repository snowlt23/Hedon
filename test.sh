#!/bin/sh

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

runtest "555" "555"
runtest "4 5 +" "9"
runtest "6 5 -" "1"
runtest "0 1 -" "-1"
runtest ": add5 5 + ; 4 add5" "9"
runtest ": drop X 0x48 X 0x83 X 0xc3 X 0x08 ; 4 5 drop" "4"
runtest ": add5 5 + ; dump-label add5 0" "Num -- Num0"
runtest ": add5 5 + ; : x9 4 add5 ; x9" "9"

runtest "4 5 drop" "4"
runtest "555 . cr 0" "555
0"
runtest "9 dp @ ! dp @ @" "9"
runtest "1 2 3 pick3" "1"
runtest "0 dp @ ! 45 dp @ +! dp @ @" "45"
runtest "1 , 2 , 3 , dp @ cell - @" "3"

errortest ": main ; drop" "stack is empty, but expected Fixnum value in main"
errortest ": add5 5 + ; 5 ( drop )" "stack is empty, but expected Label value in add5"
error_filetest "examples/basic-type.hedon" "unmatch MyInt label to Num in mi"
filetest "examples/union-type.hedon" "18"
filetest "examples/variable.hedon" "090"
