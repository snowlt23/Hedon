#!/bin/sh

runtest() {
  echo "[TEST] $1"
  OUT=$(echo "$1" | ./hedon)
  if [ "$OUT" != "$2" ] ; then
    echo "[ERROR] $1    expect $2, but got $OUT"
    exit 1
  fi
}
exittest() {
  echo "[TEST] $1"
  OUT=$(echo "$1" | ./hedon)
  RET=$?
  if [ "$RET" != "$2" ] ; then
    echo "[ERROR] $1    expect $2, but got $RET"
    exit 1
  fi
}

filetest() {
  echo "[TEST] $1"
  OUT=$(./hedon -l $1 -c)
  if [ "$OUT" != "$2" ] ; then
    echo "[ERROR] $1    expect $2, but got $OUT"
    exit 1
  fi
}

errortest() {
  echo "[TEST] $1"
  ERR=$(echo "$1" | ./hedon 2>&1)
  if [ "$ERR" != "$2" ] ; then
    echo "[ERROR] $1    expect \"$2\" error, but got \"$ERR\""
    exit 1
  fi
}

error_filetest() {
  echo "[TEST] $1"
  ERR=$(./hedon -l $1 -c 2>&1)
  if [ "$ERR" != "$2" ] ; then
    echo "[ERROR] $1    expect \"$2\" error, but got \"$ERR\""
    exit 1
  fi
}

langtest() {
  ./hedon -l $1 -c
  RET=$?
  if [ "$RET" != "0" ] ; then
    echo "<<ERROR>> language test $1"
    exit 1
  fi
}

runtest "555 ." "555"
runtest "4 5 + ." "9"
runtest "6 5 - ." "1"
runtest "0 1 - ." "-1"
runtest ": main 9 . ; dump-type main" "<> -- <>"
runtest ": add5 5 + ; 4 add5 ." "9"
runtest "4 5 Int.drop ." "4"
runtest ": add5 5 + ; dump-type add5" "Int -- Int"
runtest ": add5 5 + ; : x9 4 add5 ; x9 ." "9"
runtest ": m 5 eq ; 4 m .b 5 m .b" "01"
runtest ": main 8 4 / . ; main" "2"
runtest ": main 8 5 mod . ; main" "3"

runtest "4 5 Int.drop ." "4"
runtest "555 . cr 0 ." "555
0"
runtest ": main 4 drop 5 ; main ." "5"
runtest ": main 1 dup dup + + ; main ." "3"
runtest ": 2dup dup dup ; dump-type 2dup : main 1 2dup + + ; main ." "a -- a a a3"
runtest ": 2dup dup dup ; : t2dup Int dup dup ; : main 1 2dup + + ; main ." "3"
runtest ": 2dup dup dup ; : 3dup dup 2dup ; : main 1 3dup + + + ; main ." "4"
runtest ": main 1 2 3 rot . . . ; main" "213"
runtest "1 2 3 Int.pick3 ." "1"
runtest ": main 0 9 dp Int.pick3 drop p@ ! dp p@ @ . ; main" "9"
runtest "9 dp p@ ! dp p@ @ ." "9"
runtest "0 dp p@ ! 45 dp p@ +! dp p@ @ ." "45"
runtest "1 , 2 , 3 , dp@ cell p- @ ." "3"
# runtest "9 const nine nine ." "9"
runtest "var fz Pointer 555 fz ! fz @ ." "555"
runtest "var aaa Pointer var bbb Pointer 123 aaa ! 456 bbb ! aaa @ . bbb @ ." "123456"
exittest "4 . 5 . 23 exit 6 ." "23"
runtest ": hello \"Hello Yukari!\" .s ; hello" "Hello Yukari!"
# errortest ": main ; drop" "error in main: stack is empty, but expected a value at drop"
error_filetest "examples/basic-type.hedon" "error in mi: unmatch MyInt type to Int at +"

# data-flow
runtest ": main 2 3 [ 2 + ] dip ; main . ." "34"
runtest ": main 3 4 [ + ] keep ; main . ." "47"
runtest ": main 1 [ 2 + ] [ 3 + ] bi ; main . ." "43"
runtest ": main 4 3 1 [ + ] [ + ] bi ; main . ." "54"
runtest ": main 1 2 [ 2 + ] bi@ ; main . ." "43"
runtest ": next [ 1 + ] dip Pointer.drop ; dump-type next" "Int Pointer -- Int"

# control flow
runtest ": main 8 false [ 4 . ] when ; main ." "8"
runtest ": main 8 true [ 4 . ] when ; main ." "48"
runtest ": main false [ 4 . ] when true [ 5 . ] when ; main" "5"
runtest ": main false [ 4 . ] when true [ 5 . ] when ; dump-type main" "<> -- <>"
errortest "0 : main false when ;" "error in main: stack is empty, but expected Quot value at when"
errortest ": main [ 5 . ] when ; dump-type main false main true main" "Bool -- <>5"
errortest ": main true [ 4 ] when ; main" "error in main: <> <-> Int unmatch out-effect at when"
runtest ": main true [ 4 . ] [ 5 . ] if false [ 4 . ] [ 5 . ] if ; main" "45"
runtest ": main false [ 4 ] [ 5 ] if ; dump-type main main ." "<> -- Int5"
runtest ": main false [ 4 ] [ 5 ] if . 6 . ; main" "56"
runtest ": nnot [ false ] [ true ] if ; true nnot .b false nnot .b" "01"
errortest ": main true [ 4 ] [ ] if ; main" "error in main: Int <-> <> unmatch out-effect at if"
errortest ": main true [ . ] [ ] if ; main" "error in main: Int <-> <> unmatch in-effect at if"
runtest ": main 0 [ dup 10 < ] [ dup . 1 + ] while ; main" "0123456789"

filetest "examples/variable.hedon" "09"
# error_filetest "examples/err.hedon" "error in main2: unresolved drop trait word at drop"

# linear logic
error_filetest "examples/linear.hedon" "<> -- <>
<> -- l.File"

# macro
runtest ": main 4 5 2dup . . . . ; main" "5454"
runtest ": main swap Int.drop Cstr.drop ; dump-type main" "Int Cstr -- <>"
runtest ": main 2dup Int.drop Cstr.drop ; dump-type main" "Cstr Int -- Cstr Int"
runtest ": main 5 0 [ 2dup > ] [ dup . 1+ ] while ; main" "01234"
runtest ": main 5 swap [ ] do ; dump-type main" "Int -- <>"
runtest ": main 0 5 [ dup . ] do ; main" "01234"
runtest "array K Pointer 5 , 4 , 5 , : main 0 K array@ . 1 K array@ . 2 K array@ . ; main" "545"
# runtest ": run-test \"aaa\" 1 1 eq test ; run-test 9 ." "[OK] aaa
# 9"
# runtest ": run-test \"0eq1\" 0 1 eq test ; run-test 9 ." "[ERROR] 0eq1"
runtest "dump-type to-pointer" "Fixnum -- Pointer"
runtest ": 555p 555 to-pointer ; : main 555p to-int . ; dump-type 555p main" "<> -- Pointer555" # cast

# vocab
error_filetest "examples/vocab.hedon" "error in main: undefined helloworld word at helloworld"

# cffi
runtest ": main 9 test.call1 c.call1 . ; main" "9"
runtest ": main 1 2 test.call2 c.call2 . ; main" "-1"
runtest ": main 1 2 3 4 5 6 test.call6 c.call6 . ; main" "-19"

# string
runtest ": main \"yukarin\" strlen . ; main" "7"
runtest ": main 7 new-cstr [ \"yukarin\" strcpy ] keep .s ; main" "yukarin"
runtest ": main \"yuka\" string .ss ; main" "yuka"
runtest ": main \"yuka\" string length>> . ; main" "4"
runtest ": main \"yuka\" string \"maki\" string length>> . ; main" "4"
runtest ": main \"seki\" string \"ro\" strlen . length>> . ; main" "24"
runtest ": main \"seki\" string \"ro\" string 2print ; main" "sekiro"
runtest ": main \"seki\" string \"ro\" string 2length-print ; main" "42"
runtest ": main \"yuka\" string \"maki\" string 2print ; main" "yukamaki"
runtest ": main \"yuka\" string \"maki\" string 2length . ; main" "8"
runtest ": main \"yuka\" string \"maki\" string concat .ss ; main" "yukamaki"
runtest ": main \"kiri\" \"kizu\" \"aka\" \"maki\" \"yuka\" string append-cstr append-cstr append-cstr append-cstr .ss ; main" "yukamakiakakizukiri"
runtest ": main \"yuka\" \"kiri\" streq? .b \"yuka\" \"yuka\" streq? .b ; main" "01"

# file
rm test.txt
touch test.txt
filetest "examples/fileio.hedon" "yukayuka"

# macro by using string
filetest "examples/word-by-string.hedon" "invoke Int.origin"

# record

echo "<<Language Test>>"
# langtest "tests/basic.hedon"
# langtest "tests/controlflow.hedon"
# langtest "tests/vocab.hedon"
# langtest "tests/linear.hedon"
# langtest "tests/macro.hedon"
# langtest "tests/cffi.hedon"
# langtest "tests/string.hedon"
# langtest "tests/fileio.hedon"
