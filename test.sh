#!/bin/sh

runtest() {
  OUT=$(echo "$1" | ./hedon -l prelude.hedon)
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
  ERR=$(echo "$1" | ./hedon -l prelude.hedon 2>&1)
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
runtest "dump-effect +" "Int:in Int:in Int:out "
runtest "6 5 - ." "1"
runtest "0 1 - ." "-1"
runtest ": add5 5 + ; 4 add5 ." "9"
runtest "4 5 Int.drop ." "4"
runtest ": add5 5 + ; dump-type add5" "Int.t -- Int.t"
runtest ": add5 5 + ; dump-effect add5" "Int:out Int:in Int:in Int:out "
runtest ": add5 5 + ; : x9 4 add5 ; x9 ." "9"

runtest "4 5 Int.drop ." "4"
runtest "555 . cr 0 ." "555
0"
runtest "9 dp p@ ! dp p@ @ ." "9"
runtest ": main 4 drop 5 ; main ." "5"
runtest ": main 1 dup dup + + ; main ." "3"
runtest ": 2dup dup dup ; dump-type 2dup : main 1 2dup + + ; main ." "a -- a a a3"
runtest ": 2dup dup dup ; : t2dup Int dup dup ; : main 1 2dup + + ; main ." "3"
runtest ": 2dup dup dup ; dump-effect 2dup" "dup.eff:in dup.eff:in "
runtest ": 2dup dup dup ; : 3dup dup 2dup ; : main 1 3dup + + + ; main ." "4"
runtest "1 2 3 Int.pick3 ." "1"
runtest ": main 0 9 dp Int.pick3 drop p@ ! dp p@ @ . ; main" "9"
runtest "0 dp p@ ! 45 dp p@ +! dp p@ @ ." "45"
runtest "1 , 2 , 3 , dp@ cell p- @ ." "3"
# runtest "9 const nine nine ." "9"
runtest "var fz !( -- Pointer ) 555 fz ! fz @ ." "555"
runtest "var aaa !( -- Pointer ) var bbb !( -- Pointer ) 123 aaa ! 456 bbb ! aaa @ . bbb @ ." "123456"
runtest "4 . 5 . 0 exit 6 ." "45"
runtest ": main 9 . ; dump-type main" " -- "

# control flow
runtest ": main 0 if 4 . then 1 if 5 . then ; main" "5"
runtest ": main 0 if 4 . then 1 if 5 . then ; dump-type main" " -- "
runtest ": main 1 if 4 . else 5 . then 0 if 4 . else 5 . then ; main" "45"
runtest ": main 0 if 4 else 5 then ; dump-type main main ." " -- Int.t5"
errortest ": main 1 if 4 else then ; main" "Int.t <->  unmatch out-effect in main"
errortest ": main 1 if . else then ; main" "Int.t <->  unmatch in-effect in main"
errortest "0 : main 1 then ; main" "unmatch Int.t type to IfAddr.t in main"
runtest ": main 0 begin dup 10 - while dup . 1 + repeat ; main" "0123456789"

errortest ": main ; drop" "unresolved drop trait word in main"
error_filetest "examples/basic-type.hedon" "unmatch MyInt type to Int.t in mi"
# filetest "examples/variable.hedon" "09"
filetest "examples/postpone.hedon" "9"
error_filetest "examples/err.hedon" "unresolved drop trait word in main2"

# linear logic
error_filetest "examples/linear.hedon" "unmatch FileU.t type to File.t in write-file-illegal"

# cffi
runtest ": main 1 2 c.sub c.call2 . ; main" "-1"
runtest ": main 1 2 3 4 5 6 ccc.call6 c.call6 . ; main" "-19"
filetest "examples/dl.hedon" "555"
