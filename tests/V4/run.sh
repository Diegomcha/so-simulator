#!/bin/bash

# Compiling the project
make

# Run the tests
echo "[V4] Running tests..."

./Simulator --debugSections=a --daemonsProgramsFile=teachersDaemonsTest --memConfigFile=MemConfigTest1 programVerySimpleTest 100 | grep -e BLOCKED -e END > tests/V4/obtained_outputs/test1.log
./Simulator --debugSections=a --memConfigFile=MemConfigTest2 test2-1 10 test2-2 30 iDontExist 350 | grep -e BLOCKED -e END -e ERROR > tests/V4/obtained_outputs/test2.log
./Simulator --debugSections=a --numProcesses=8 --initialPID=5 --memConfigFile=MemConfigTest2 test3-1 10 test3-2 11 test3-3 12 | grep -e exception -e END -e transfer > tests/V4/obtained_outputs/test3.log
./Simulator --debugSections=a --memConfigFile=MemConfigTest2 test4-1 10 test4-2 11 | grep -e "MOV 1 0" -e transfer -e BLOCK -e END > tests/V4/obtained_outputs/test4.log
./Simulator --debugSections=a --daemonsProgramsFile=daemonsFileScheduling --memConfigFile=MemConfigTest3 test5-1 10 | grep -e thrown -e END -e BLOCKED -e created > tests/V4/obtained_outputs/test5.log

# Compare the outputs
echo "[V4] Comparing outputs..."
echo "[V4] test1.log" | less
diff --color=always tests/V4/obtained_outputs/test1.log tests/V4/expected_outputs/test1.log | less -R
echo "[V4] test2.log" | less
diff --color=always tests/V4/obtained_outputs/test2.log tests/V4/expected_outputs/test2.log | less -R
echo "[V4] test3.log" | less
diff --color=always tests/V4/obtained_outputs/test3.log tests/V4/expected_outputs/test3.log | less -R
echo "[V4] test4.log" | less
diff --color=always tests/V4/obtained_outputs/test4.log tests/V4/expected_outputs/test4.log | less -R
echo "[V4] test5.log" | less
diff --color=always tests/V4/obtained_outputs/test5.log tests/V4/expected_outputs/test5.log | less -R