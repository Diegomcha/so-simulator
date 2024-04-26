#!/bin/bash

# Compiling the project
make

# Run the tests
echo "[V2] Running tests..."
./Simulator --debugSections=a programV2-fa > tests/obtained_outputs/programV2-fa.log
./Simulator --debugSections=a programV2-fb > tests/obtained_outputs/programV2-fb.log
./Simulator --debugSections=a programV2-fa programV2-fc > tests/obtained_outputs/programV2-fa_programV2-fc.log
./Simulator --debugSections=a programV2-fa iDontExist programV2-fb programV2-fc > tests/obtained_outputs/programV2-fa_iDontExist_programV2-fb_programV2-fc.log

# Compare the outputs
echo "[V2] Comparing outputs..."
echo "[V2] programV2-fa.log" | less
diff --color=always tests/obtained_outputs/programV2-fa.log tests/expected_outputs/programV2-fa.log | less -R

echo "[V2] programV2-fb.log" | less
diff --color=always tests/obtained_outputs/programV2-fb.log tests/expected_outputs/programV2-fb.log | less -R

echo "[V2] programV2-fa_programV2-fc.log" | less
diff --color=always tests/obtained_outputs/programV2-fa_programV2-fc.log tests/expected_outputs/programV2-fa_programV2-fc.log | less -R

echo "[V2] programV2-fa_iDontExist_programV2-fb_programV2-fc.log" | less
diff --color=always tests/obtained_outputs/programV2-fa_iDontExist_programV2-fb_programV2-fc.log tests/expected_outputs/programV2-fa_iDontExist_programV2-fb_programV2-fc.log | less -R