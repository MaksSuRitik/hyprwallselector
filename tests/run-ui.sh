#!/bin/sh
# All test windows belong to a private virtual display, never the user's session.
set -eu
exec xvfb-run -a -s '-screen 0 1280x800x24 -nolisten tcp' \
 env QT_QPA_PLATFORM=xcb HWS_TEST_DISPLAY=isolated LIBGL_ALWAYS_SOFTWARE=1 \
 QSG_RENDER_LOOP=basic "$@"
