#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."

echo "Compiling analyzer tests..."
g++ -I "$PROJECT_ROOT/include" -I "$PROJECT_ROOT/include/driver/kissfft" -I "$PROJECT_ROOT/include/driver" -I "$SCRIPT_DIR/mocks" "$SCRIPT_DIR/test_analyzer.cpp" "$PROJECT_ROOT/src/driver/kissfft/kiss_fft.cpp" "$PROJECT_ROOT/src/driver/kissfft/kiss_fftr.cpp" -o "$SCRIPT_DIR/test_analyzer_runner"

echo "Running analyzer tests..."
"$SCRIPT_DIR/test_analyzer_runner"

echo "Cleaning up..."
rm "$SCRIPT_DIR/test_analyzer_runner"

echo "Tests passed successfully!"
