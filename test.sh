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
  OUT=$(./hedon -l prelude.hedon -l $1 -c)
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
  ERR=$(./hedon -l prelude.hedon -l $1 -c 2>&1)
  echo "[TEST] $1"
  if [ "$ERR" != "$2" ] ; then
    echo "[ERROR] $1    expect \"$2\" error, but got \"$ERR\""
    exit 1
  fi
}

runtest "555 ." "555"
runtest "4 5 + ." "9"
runtest "6 5 - ." "1"
runtest "0 1 - ." "-1"
runtest ": add5 5 + ; 4 add5 ." "9"
runtest "4 5 drop ." "4"
runtest ": add5 5 + ; dump-label add5" "Fixnum -- Fixnum"
runtest ": add5 5 + ; : x9 4 add5 ; x9 ." "9"

runtest "4 5 drop ." "4"
runtest "555 . cr 0 ." "555
0"
runtest "9 dp @ ! dp @ @ ." "9"
runtest "1 2 3 pick3 ." "1"
runtest "0 dp @ ! 45 dp @ +! dp @ @ ." "45"
runtest "1 , 2 , 3 , dp @ cell - @ ." "3"
runtest "9 const nine nine ." "9"
runtest "4 . 5 . 0 exit 6 ." "45"
runtest ": main 0 if 4 . then 1 if 5 . then ; main" "5"

errortest ": main ; drop" "stack is empty, but expected Fixnum value in main"
errortest ": add5 5 + ; 5 ( drop )" "stack is empty, but expected Label value in add5"
error_filetest "examples/basic-type.hedon" "unmatch MyInt label to Fixnum in mi"
filetest "examples/variable.hedon" "09"
error_filetest "examples/err.hedon" "unmatch Err label to Fixnum in main2"
