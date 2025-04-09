#!/bin/sh
# NOTE: becomes .tmp_update/my355.sh

PLATFORM="my355"
SDCARD_PATH="/mnt/SDCARD"
UPDATE_PATH="$SDCARD_PATH/MinUI.zip"
SYSTEM_PATH="$SDCARD_PATH/.system"

CPU_PATH=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo performance > "$CPU_PATH"

# install/update
if [ -f "$UPDATE_PATH" ]; then 
	echo ok
	export LD_LIBRARY_PATH=/usr/trimui/lib:$LD_LIBRARY_PATH
	export PATH=/usr/trimui/bin:$PATH

	# leds_off
	echo 0 > /sys/class/led_anim/max_scale
	
	TRIMUI_MODEL=`strings /usr/trimui/bin/MainUI | grep ^Trimui`
	if [ "$TRIMUI_MODEL" = "Trimui Brick" ]; then
		DEVICE="brick"
	fi
	
	cd $(dirname "$0")/$PLATFORM
	if [ -d "$SYSTEM_PATH" ]; then
		./show.elf ./$DEVICE/updating.png
	else
		./show.elf ./$DEVICE/installing.png
	fi

	# clean replacement for core paths
	rm -rf $SYSTEM_PATH/$PLATFORM/bin
	rm -rf $SYSTEM_PATH/$PLATFORM/lib
	rm -rf $SYSTEM_PATH/$PLATFORM/paks/MinUI.pak

	./unzip -o "$UPDATE_PATH" -d "$SDCARD_PATH" # &> /mnt/SDCARD/unzip.txt
	rm -f "$UPDATE_PATH"

	# the updated system finishes the install/update
	if [ -f $SYSTEM_PATH/$PLATFORM/bin/install.sh ]; then
		$SYSTEM_PATH/$PLATFORM/bin/install.sh # &> $SDCARD_PATH/log.txt
	fi
	
fi

LAUNCH_PATH="$SYSTEM_PATH/$PLATFORM/paks/MinUI.pak/launch.sh"
while [ -f "$LAUNCH_PATH" ] ; do
	"$LAUNCH_PATH"
done

poweroff # under no circumstances should stock be allowed to touch this card