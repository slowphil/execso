# `exec.so` for mikutter AppImage
mikutterのAppImageでブラウザを開くためのワークアラウンド．

---
You should also put `exec.so` into `usr/optional`. This exec.so library is intended to restore the environment of the AppImage to its parent.
This is done to avoid library clashing of bundled libraries with external processes. e.g when running the web browser

The intended usage is as follows:

1. This library is injected to the dynamic loader through LD_PRELOAD
   automatically in AppRun **only** if `usr/optional/exec.so` exists: 
   e.g `LD_PRELOAD=$APPDIR/usr/optional/exec.so My.AppImage`

2. This library will intercept calls to new processes and will detect whether
   those calls are for binaries within the AppImage bundle or external ones.

3. In case it's an internal process, it will not change anything.
   In case it's an external process, it will restore the environment of
   the AppImage parent by reading `/proc/[pid]/environ`.
   This is the conservative approach taken.
