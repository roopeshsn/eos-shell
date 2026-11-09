#!/bin/sh
# Golden-output tests: each tests/golden/NAME.in is fed to eosh and compared with NAME.out.
cd "$(dirname "$0")/.." || exit 1
fail=0
for in in tests/golden/*.in; do
    name=$(basename "$in" .in)
    out=$(EOSH_HOSTNAME=sw1 ./eosh < "$in" 2>&1)
    if [ "$out" = "$(cat "tests/golden/$name.out")" ]; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        echo "$out" | diff - "tests/golden/$name.out"
        fail=1
    fi
done
exit $fail
