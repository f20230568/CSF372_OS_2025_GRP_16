# Shell Tests

This directory contains tests for the Pintos shell implementation.

## Test Overview

The shell tests verify the functionality of the basic shell implementation in `src/examples/shell.c`, with a focus on testing fundamental edge cases and robustness of the echo command.

## Test Files

### Current Tests
- **echo-none**: Tests echo command with no arguments (basic functionality)
- **echo-multi**: Tests echo with multiple arguments and complex argument parsing

#### Echo Robustness Tests (Split from echo-malformed)
- **echo-whitespace**: Tests echo with whitespace-only arguments
- **echo-mixed**: Tests echo with mixed whitespace (leading/trailing spaces)
- **echo-newlines**: Tests echo with newline and carriage return characters
- **echo-dashes**: Tests echo with dash-prefixed arguments (--help, -n, etc.)
- **echo-shell-chars**: Tests echo with shell interpretation characters ($(), ``, $HOME)

### Test Categories

#### Fundamental Functionality Tests
- Basic argument parsing
- Multiple argument handling
- Whitespace and special character handling

#### Robustness Tests
- Boundary condition testing
- Memory corruption detection
- Malformed input handling

## Test Structure

Each test consists of:
- A `.c` source file that implements the test logic
- A `.ck` check file that defines expected output
- Integration in the Make.tests build system

## Running Tests

To run the shell tests:

```bash
cd pintos/src/userprog
make check
```

Or to run specific shell tests:

```bash
cd pintos/src/userprog
make tests/shell/echo-none.result
make tests/shell/echo-whitespace.result
```

## Test Dependencies

The shell tests require:
- A working userprog kernel
- The shell executable to be built
- Basic system calls (exec, wait, chdir) to be implemented

## Expected Behavior

The shell should:
1. Handle basic echo commands correctly
2. Process command line arguments robustly
3. Handle extreme input gracefully without crashing
4. Prevent buffer overflows and memory corruption
5. Process malformed input safely
6. Return appropriate exit codes

## Test Philosophy

These tests are designed to:
- **Find fundamental bugs**: Test edge cases that reveal core implementation flaws
- **Prevent crashes**: Ensure the shell handles extreme input gracefully
- **Test robustness**: Verify the shell doesn't corrupt memory or overflow buffers
- **Cover real scenarios**: Test conditions that could occur in actual usage
- **Modular testing**: Each test focuses on a specific aspect for easier debugging and maintenance

## Notes

- The tests focus on command-line argument processing and robustness
- All tests expect the shell to exit cleanly with appropriate status codes
- Robustness tests are designed to reveal memory corruption and buffer overflow issues
- Echo robustness tests ensure the shell handles various input patterns gracefully
- Each test is focused on a specific input pattern for better isolation and debugging
