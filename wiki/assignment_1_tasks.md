# Assignment 1 Tasks

This document contains the main tasks for enhancing the Pintos shell functionality. Each task focuses on implementing robust shell commands with proper argument handling.

## Prerequisites

Before starting these tasks, ensure you have:
- Basic understanding of [shell programming concepts](./introduction_to_shell.md)
- Successfully completed the [environment setup](./assignment_1_environment_setup.md)
- Access to the Pintos development environment

## Task 1: Implement Robust echo Command

**Objective:** Implement a robust echo command that handles various argument types and edge cases gracefully.

### Requirements
The echo command should:
- Handle no arguments gracefully (exit with status 0)
- Process multiple arguments correctly
- Handle arguments with special characters (quotes, backslashes, tabs)
- Process arguments with leading/trailing spaces
- Handle newline and carriage return characters in arguments
- Process dash-prefixed arguments (--help, -n, --version)

### Testing Scenarios
Your implementation should pass these test cases:
- **echo-none**: No arguments - should exit successfully
- **echo-multi**: Multiple arguments, mixed content, many arguments
- **echo-mixed**: Arguments with leading/trailing whitespace
- **echo-newlines**: Arguments containing newline and carriage return characters
- **echo-dashes**: Arguments starting with dashes

### Expected Behavior
Refer to the .ck files in the `tests/shell` folder.

## Task 2: Implement touch Command

**Objective:** Implement a `touch` command that creates empty files.

### Requirements
- Create a new `touch` command that accepts a filename as an argument
- The command should create an empty file if it doesn't exist
- If the file already exists, the command should succeed (no error)
- The command should exit with status 0 on success

### Testing
Your implementation should pass the **touch-create** test:
- Running `touch newfile.txt` should create an empty file
- The shell should report success and exit with status 0
- The created file should exist and be empty (0 bytes)

### Expected Behavior
```
$ touch newfile.txt
newfile.txt: created
touch: exit(0)
$ ls
newfile.txt
$ cat newfile.txt
(empty file)
```

## Implementation Notes

### Shell Robustness
- All commands should handle malformed input gracefully
- Prevent buffer overflows and memory corruption
- Return appropriate exit codes
- Handle extreme input patterns safely

### Testing
Run the shell tests to verify your implementation:
```bash
cd pintos/src/userprog
make check
```

Or run specific tests:
```bash
make tests/shell/echo-none.result
make tests/shell/echo-multi.result
make tests/shell/touch-create.result
```