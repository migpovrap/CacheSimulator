#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)"

EXPECTED_OUTPUTS_TARGET_DIR="tlbsim-l2"

cd $SCRIPT_DIR
mkdir -p reports

make -j

for input in inputs/*; do
    input_file=$(basename "$input" .txt)

    expected_output_file=outputs/$EXPECTED_OUTPUTS_TARGET_DIR/$input_file.out
    report_file=reports/$input_file.diff

    if [ ! -f "$expected_output_file" ]; then
        echo "Expected output file $expected_output_file not found"
        exit 1
    fi

    echo "Running test for $input_file -> $report_file"
    ./build/tlbsim $input > reports/$input_file.out 2> /dev/null
    ./build/tlbsim $input > reports/$input_file.log 2>&1

    echo "#####################################################################" > $report_file
    echo "# Input: $input" >> $report_file
    echo "# Left side: expected ($expected_output_file)" >> $report_file
    echo "# Right side: actual (reports/$input_file.out)" >> $report_file
    echo "#####################################################################" >> $report_file

    if diff -y --expand-tabs $expected_output_file reports/$input_file.out >> $report_file; then
        echo "# Test $input_file passed" >> $report_file
    else
        echo "# Test $input_file failed" >> $report_file
    fi
done