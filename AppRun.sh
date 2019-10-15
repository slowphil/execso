#!/bin/sh


#cd "$(dirname "$0")"
if [ -z $APPDIR ]; then APPDIR=$(readlink -f $(dirname "$0")); export APPDIR; fi



LD_LIBRARY_PATH="$APPDIR"/usr/lib
export LD_LIBRARY_PATH
#export LD_PRELOAD="$APPDIR"/usr/optional/exec.so:"$LD_PRELOAD"
LD_PRELOAD="$APPDIR"/usr/optional/exec.so
export LD_PRELOAD

#export QT_PLUGIN_PATH="$APPDIR"/usr/plugins

#exec="$(sed -n 's|^Exec=||p' $(ls -1 *.desktop))"

#$exec "$*"
#exit $?

prefix="$APPDIR"/usr/
exec_prefix="${prefix}"
datarootdir="${prefix}/share"
datadir="${datarootdir}"

TEXMACS_PATH=${datarootdir}/TeXmacs
export TEXMACS_PATH
TEXMACS_BIN_PATH=${exec_prefix}
export TEXMACS_BIN_PATH

export PATH="$TEXMACS_BIN_PATH/bin:$APPDIR"/usr/bin/:"$PATH"


DISTRIBUTOR_ID=`lsb_release -a 2>/dev/null |grep "Distributor ID"`
if [ ! "${DISTRIBUTOR_ID#*Ubuntu}" = "$DISTRIBUTOR_ID" ] ; then
  export QT_X11_NO_NATIVE_MENUBAR=1
fi


exec texmacs.bin "$@" < /dev/null
