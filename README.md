# AutoLaunch 🚀

![AutoLaunch process](https://github.com/strinque/AutoLaunch/blob/master/docs/tasks-process.gif)

The **AutoLaunch** **Windows** application allows users to automatically execute **tasks** using a `json` file.  
An example of a `json` **tasks** file has been provided in the model directory.
Implemented in c++17 and use `vcpkg`/`cmake` for the build-system.  

It uses the `winpp` header-only library from: https://github.com/strinque/winpp.

## Features

- [x] use `nlohmann/json` header-only library for `json` parsing
- [x] handle command-line argument variables
- [x] handle defined variables in tasks.json file
- [x] handle variables that can be parsed from the output of an executable using regex
- [x] handle a list of task flags such as `debug`, `display`, `ask-execute`, `ask-continue`, `ignore-error`, `protect`

## Usage

![AutoLaunch help](https://github.com/strinque/AutoLaunch/blob/master/docs/help.png)

The following example executes a list of simple tasks to compress a directory.  
It demonstrate the use of most of the features of the **AutoLaunch** program.

``` console
# execute a set of tasks using json file
AutoLaunch.exe --tasks tasks.json \
               --variables "dir:$[C:\Windows\System32]" \
               --interactive
```

``` json
{
  "description": "Complete set of tasks to compress directory with ${dir}",
  "variables": [
    {"7-zip": "$[C:\\Program Files\\7-Zip\\7z.exe]"}
  ],
  "tasks": [
    {
      "description": "list all files and directories in ${dir}",
      "cmd": "powershell.exe",
      "args": "ls ${dir}",
      "parse-variables": [
        {"filter-dir": "[0-9]* (WindowsPower[^ ]*)"}
      ],
      "variables": [
        {"input-dir": "$[${dir}/${filter-dir}]"}
      ],
      "ask-continue": true
    },
    {
      "description": "get current date to format log file",
      "cmd": "powershell.exe",
      "args": "-Command \"Get-Date -Format 'yyyy-MM-dd HH-mm-ss'\"",
      "parse-variables": [
        {"current-date": "([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}-[0-9]{2}-[0-9]{2})"}
      ],
      "variables": [
        {"archive": "$[archive - ${current-date}.zip]"}
      ]
    },
    {
      "description": "compress directory: ${input-dir} with 7z",
      "cmd": "${7-zip}",
      "args": "a ${archive} ${input-dir}",
      "display": true
    }
  ]
}
```

## Description

### Task flags

Several optional flags can be defined for each task: 

- `debug`: don't execute the task, only display the generated command-line for debugging (overwrites the command-line `debug` value)
- `ask-execute`: prompt user if this task needs to be executed (overwrites the command-line `ask-execute` value)
- `ask-continue`: prompt user if the program should continue after this step
- `ignore-error`: ignore program return code (when program is expected to fail in particular conditions)
- `protected`: these task use a shared resource and should be protected (executed only one at a time between all **AutoLaunch** instances)
- `display`: display the program output

### Variables

<h3><code>command-line variables</code></h3>

The `variables` will be replaced by their values during runtime execution.  

Command-line `variables` can be given to the program to customize the tasks execution sequence.  
These `variables` should be defined using the format `key:value` and separated from each-others by `;` character.

``` console
--variables "product-name:test;version:5000"
```

A special variable: `debug:true` can be set to debug the execution of all the tasks at once.  
Another special variable: `ask-execute:true` can be set to ask for each task if it needs to be executed.  

<h3><code>json variables</code></h3>

It's also possible to define `variables` in the **tasks** `json` file.  
Either in the global `"variables"` json section or in each `"task"` section.  

``` console
# define a variable: xxx with value: "this is a test"
{"xxx": "this is a test"}

# using the variable
"output: ${xxx}" => replace "${xxx}" by it's value (ex: "output: this is a test")
"output: ${xxx, ' ', '_'}" => replace ${xxx, ' ', '_'} by it's value then convert all characters ' ' by '_' (ex: "output: this_is_a_test")
```

### Absolute path

Another replacement method has been implemented which replaces path by their absolute path.  

```console
# using absolute path convertion
"output: $[../file.json] => convert the "../file.json" path to absolute path surrounded by double-quote (ex: "output: \"c:\test\file.json\"")
"output: $<../file.json> => convert the "../file.json" path to absolute path surrounded by single-quote (ex: "output: 'c:\test\file.json'")

# define a path in a variable
{"xxx": "../file.json"},

# mixing variable and absolute path system
"output: $[${xxx}]" => replace the xxx variable then convert it's value to absolute path (ex: "output: \"c:\test\file.json\"")
```

### Runtime variables

It's also possible to create `variables` by parsing the output of the task execution and using regex to determine their values.

```json
    {
      "description": "get current date to format log file",
      "cmd": "powershell.exe",
      "args": "-Command \"Get-Date -Format 'yyyy-MM-dd HH-mm-ss'\"",
      "parse-variables": [
        {"current-date": "([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}-[0-9]{2}-[0-9]{2})"}
      ],
      "variables": [
        {"archive": "$[archive - ${current-date}.zip]"}
      ]
    }
```

## Requirements

This project uses **vcpkg**, a free C/C++ package manager for acquiring and managing libraries to build all the required libraries.  
It also needs the installation of the **winpp**, a private *header-only library*, inside **vcpkg**.

### Install vcpkg

The install procedure can be found in: https://vcpkg.io/en/getting-started.html.  
The following procedure installs `vcpkg` and integrates it in **Visual Studio**.

Open PowerShell: 

``` console
cd c:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe integrate install
```

Create a `x64-windows-static-md` triplet file used to build the program in *shared-mode* for **Windows CRT** libraries but *static-mode* for third-party libraries:

``` console
$VCPKG_DIR = Get-Content "$Env:LocalAppData/vcpkg/vcpkg.path.txt" -Raw 

Set-Content "$VCPKG_DIR/triplets/community/x64-windows-static-md.cmake" 'set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)'
```

### Install winpp ports-files

Copy the *vcpkg ports files* from **winpp** *header-only library* repository to the **vcpkg** directory.

``` console
$VCPKG_DIR = Get-Content "$Env:LocalAppData/vcpkg/vcpkg.path.txt" -Raw 

mkdir $VCPKG_DIR/ports/winpp
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/strinque/winpp/master/vcpkg/ports/winpp/portfile.cmake" -OutFile "$VCPKG_DIR/ports/winpp/portfile.cmake"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/strinque/winpp/master/vcpkg/ports/winpp/vcpkg.json" -OutFile "$VCPKG_DIR/ports/winpp/vcpkg.json"
```

## Build

### Build using cmake

To build the program with `vcpkg` and `cmake`, follow these steps:

``` console
$VCPKG_DIR = Get-Content "$Env:LocalAppData/vcpkg/vcpkg.path.txt" -Raw 

git clone https://github.com/strinque/AutoLaunch
cd AutoLaunch
mkdir build; cd build
cmake -DCMAKE_BUILD_TYPE="MinSizeRel" `
      -DVCPKG_TARGET_TRIPLET="x64-windows-static-md" `
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" `
      ../
cmake --build . --config MinSizeRel
```

The program executable should be compiled in: `AutoLaunch\build\src\MinSizeRel\AutoLaunch.exe`.

### Build with Visual Studio

**Microsoft Visual Studio** can automatically install required **vcpkg** libraries and build the program thanks to the pre-configured files: 

- `CMakeSettings.json`: debug and release settings
- `vcpkg.json`: libraries dependencies

The following steps needs to be executed in order to build/debug the program:

``` console
File => Open => Folder...
  Choose AutoLaunch root directory
Solution Explorer => Switch between solutions and available views => CMake Targets View
Select x64-release or x64-debug
Select the src\AutoLaunch.exe (not bin\AutoLaunch.exe)
```

To add command-line arguments for debugging the program:

```
Solution Explorer => Project => (executable) => Debug and Launch Settings => src\program.exe
```

``` json
  "args": [
    "--tasks \"${projectDir}\\model\\tasks.json\"",
    "--variables \"dir:$[C:\\Windows\\System32]\""
  ]
```