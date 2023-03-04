#!/bin/bash

if [[ "$#" -ne 1 ]]; then
    echo "Usage: run_tests.sh path/to/t86-cli"
    exit 1
fi

set -e
set -o xtrace

for file in t86-cli/tests/*.in; do
    ref="${file%.in}.ref"
    ${1} run ${file} > "test_out.tmp"
    if ! diff "test_out.tmp" "${file%.in}.ref" >"diff_out.tmp"; then
        echo "Test ${file} failed"
        cat diff_out.tmp
        exit 1
    fi
    rm diff_out.tmp test_out.tmp
done

echo "All tests passed :-)"
