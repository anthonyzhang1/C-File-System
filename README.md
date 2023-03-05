# C File System
This is a basic file system that can create a volume, create and maintain a free space management system, initialize a root directory and maintain directory information, create, read, write, and delete files, display info, and so on. It probably only works on a Linux computer.

The C File System was created for my university's Operating Systems course in Fall 2021, where I worked on the project with 3 other students. The team's members are listed in the table below:

| Student Name       | GitHub Username   |
| :---:              | :---:             |
| Anthony Zhang      | anthonyzhang1     |
| Edel Jhon Cenario  | kurtina09         |
| Michael Wang       | michaelwang07     |
| Michael Widergren  | Mwid23            |

\
The `Documents` folder contains the prompt/instructions assigned to us by the instructor, as well as the reports and writeups we submitted. \
The `Hexdump` folder contains a tool that lets you analyze the volume created and used by the C file system. There are examples of its usage in `Documents/Milestone 1 Writeup.pdf`, but I do not have any instructions on how to use it other than that you need to use `make run` first in the `Hexdump` folder to install the tool.

#### The C file system seems to have no issues, but I do not recommend using it for anything other than testing or experimenting with it, in case it breaks and you lose all your data in it.

## The C File System's Commands:
These commands are mostly similar to their Linux counterparts. \
The command's syntax/synopsis can be shown by entering the command (e.g. `mv`) into the prompt. A detailed explanation and examples are provided in `Documents/Submission Writeup.pdf` starting from page 5.

`ls`: Displays a list of files and subdirectories in a directory. \
`cp`:	Copies a file. \
`mv`:	Moves a file or directory. \
`md`:	Makes a new directory. \
`rm`:	Deletes a file or empty directory. \
`cp2l`:	Copies a file from the C file system (the one in the terminal) to the Linux file system (your computer). \
`cp2fs`:	Copies a file from the Linux file system (your computer) to the C file system (the one in the terminal). \
`cd`:	Changes the current working directory. \
`pwd`:	Prints the current working directory. \
`history`: Prints out a list of what was previously entered into the file system's prompt. \
`help`:	Prints out a list of available commands. \
`exit`: Exits the C file system.

# Installation Instructions
**Step 1:** Clone this repository to your computer. \
**Step 2:** Change the current working directory to the `C-File-System` directory. \
**Step 3:** Enter `make run` into the terminal, which will create a volume for you the first time you run the C file system.

#### The setup is finished, and you can begin entering commands into the C file system's prompt. Note that the volume will be empty at first, so you should create new directories with `md` or copy some files from your computer to the C file system with `cp2fs` to play around with it.

You can exit the C file system with `exit`, and you can open the C file system again by entering `make run` when you are in the `C-File-System` directory.
