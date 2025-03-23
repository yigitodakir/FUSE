# FUSE
A basic filesystem implemented by using FUSE.

The filesystem is mounted by :

``` console
./memfs [mount point]
```

Where the argument "mount point" is the directory where the filesystem will be mounted at.

## Deliverables
1) The ability to create flat files and directories in the in-memory filesystem i.e, all files and directories stored in a single directory which is the root of the filesystem.
2) The ability to create hierarchical files and directories in the in-memory filesystem i.e, create files and directories in directories inside the root directory of the filesystem.
3) The ability to write data to and read data from files.
4) The ability to append data to an existing file.
5) The ability to create soft links to files.

