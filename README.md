# The problem

GNU-TeXmacs relies on external tools provided by the distribution (for instance, `Ghostscript`) and may also launch external processes for many of its possible "plugins" sessions (python, maxima, R ...)

However, when such a process external to the Appimage is launched by TeXmacs, it must not blindly inherit the process environment of the AppImage binary, and notably not the `LD_LIBRARY_PATH` which points to libraries inside the Appimage (e.g. `freetype`) and which are most likely incompatible with the distribution's libraries expected by the external process.

# The solution 

This repo provides a library `exec.so` whose purpose is to restore a suitable environment for processes that are called 
outside of the AppImage. In particular it unsets the `LD_LIBRARY_PATH` that points
to libraries bundled with the AppImage that may clash with the distribution's version
expected by the external processes. 

Usage is as follows:

1. AppRun injects this library to the dynamic loader through LD_PRELOAD 
    (e.g `export LD_PRELOAD=$APPDIR/usr/optional/exec.so`) so that it
    masks the standard UNIX execve(), system() etc. functions. 
    Apprun also sets LD_LIBRARY_PATH, typically to $APPDIR/usr/lib for the AppImage 
    executable to find its libraries.

2. This library will intercept calls to create new processes and will try to figure whether
   those calls are for binaries within the AppImage bundle or external ones.
   (note that is is not presently foolproof: if you call an script inside the appimage 
   marked as executable but with the interpreter outside of the appimage, it will not
   recognize it is an external process).  

3. In case it's an internal process, it will not change anything. In case it's an external process, the behavior depends whether the environment variable APPIMAGE_PRESERVE_ENV_PREFIX is not set or not:
 
3a. if APPIMAGE_PRESERVE_ENV_PREFIX is not set to a value remove the LD_PRELOAD , LD_LIBRARY_PATH and APPDIR entries from the environment of the new process. Any other environment variable set by the AppImaged program is thus passed onto the child process. This approach bears similarities with that used [here](https://cgit.kde.org/scratch/brauch/appimage-exec-wrapper.git/tree/exec.c), except that it requires no complicated setup (at the expense of being less capable).
 
3b. if APPIMAGE_PRESERVE_ENV_PREFIX is set to a value, we search up the process tree the first process that is not in the appimage (APPDIR not set) and copy its _initial_ environment (from /proc/<pid>/environ) to a new environment. Then, we add to the new environment any entry of the current environment that starts with the defined prefix and launch the new process with the new environment. This assumes all the env variables the program needs to pass to the child begin with that prefix. This approach is similar to that used in [mikutter/execso](https://github.com/mikutter/execso) extended to handle processes launched by `system()` and `posix_spawnp()`.
    
For TeXmacs, one could for instance set APPIMAGE_PRESERVE_ENV_PREFIX=TEXMACS_ However in TeXmacs this last approach fails when launching a Shell plugin: it needs the environment variable PS1="tmshell$ " which is not inherited from the ancestors and not prefixed by TEXMACS_ contrarily to the above assumption... The other method works fine for that plugin (and others too). Keeping the code if it can be usefull for another AppImage.
 
  Note: If the application calls "system()" for internal appimage processes, you need to embbed a shell interpreter in the AppImage under $APPDIR/bin/sh. e.g.: `cp /bin/bash $BUILD_APPDIR/bin ; ln -srT $BUILD_APPDIR/bin/bash $BUILD_APPDIR/bin/sh`


# Building exec.so and assembling the Appimage

Clone and run make to build `exec.so`. Place it into `usr/optional` of the AppImage tree, and use the AppRun script (adapt it to your needs, this one is specific for TeXmacs) instead of standard AppRun binary. The steps for making the AppImage using `linuxdeployqt` with these binaries are detailed [here](https://github.com/darealshinji/AppImageKit-checkrt/issues/1#issuecomment-370134384) (skip the `libstdc++` copying).

You can use the readily built binaries in the releases too.

