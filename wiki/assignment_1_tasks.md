# Assignment 1 Tasks

This document contains the three main tasks for enhancing the Pintos shell functionality. Each task builds upon the previous one and introduces new concepts in shell programming.

## Prerequisites

Before starting these tasks, ensure you have:
- Basic understanding of [shell programming concepts](./introduction_to_shell.md)
- Successfully completed the [environment setup](./assignment_1_environment_setup.md)
- Access to the Pintos development environment

## Task 1: Fix echo Command

**Objective:** Modify the echo command to display only the arguments, not the command name.

### Current Behavior
When you run the command `echo hi`, the shell prints `echo hi` (includes the command name in the output).

### Required Fix
Modify the echo command implementation to only print the arguments, not the command name itself. After your fix, running `echo hi` should only print `hi` to the console.

### Testing
Test your implementation with these scenarios:
- Single argument: `echo hello`
- Multiple arguments: `echo hello world`
- No arguments: `echo`
- Special characters: `echo "Hello World"`

### Expected Output
```
$ echo hello
hello
$ echo hello world
hello world
$ echo
(no output)
```

## Task 2: Create touch Command

**Objective:** Implement a new `touch` command that creates empty files or updates file timestamps.

### Task Requirements
- Implement a new `touch` command that creates empty files
- The command should accept a filename as an argument: `touch filename.txt`
- If the file already exists, the command should update the file's timestamp
- If the file doesn't exist, create a new empty file

### Testing
Test your implementation with these scenarios:
- Creating new files: `touch newfile.txt`
- Updating existing files: `touch existingfile.txt`
- Multiple files: `touch file1.txt file2.txt`
- Invalid paths: `touch /invalid/path/file.txt`

### Expected Behavior
```
$ touch newfile.txt
$ ls
newfile.txt
$ touch existingfile.txt
(updates timestamp)
$ touch file1.txt file2.txt
(creates both files)
```

## Task 3: Add Output Redirection

**Objective:** Implement output redirection for the shell using the `>` operator.

### Feature Description
Implement output redirection for the shell using the `>` operator.

### Expected Behavior
- Running `echo hi > filename.txt` should redirect the output "hi" to the file `filename.txt` instead of displaying it on the console
- If the file already exists, it should be overwritten
- If the file doesn't exist, it should be created

### Testing Strategy
Test your implementation with these scenarios:
- Basic redirection: `echo hello > output.txt`
- Overwriting existing files: `echo world > output.txt`
- Multiple commands: `ls > filelist.txt`

### Expected Behavior
```
$ echo hello > output.txt
$ cat output.txt
hello
$ echo world > output.txt
$ cat output.txt
world
$ ls > filelist.txt
$ cat filelist.txt
(output of ls command)
```