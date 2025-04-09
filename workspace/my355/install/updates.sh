#!/bin/sh

# becomes /.system/my355/bin/install.sh

# clean up from an previous ill-considered update
DTB_PATH=/storage/.system/my355/dat/rk3566-magicx-linux.dtb
if [ -f "$DTB_PATH" ]; then
	rm -rf "$DTB_PATH"
fi