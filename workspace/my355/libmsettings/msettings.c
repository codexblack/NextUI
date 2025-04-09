#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <string.h>

#include "msettings.h"

///////////////////////////////////////

#define JACK_STATE_PATH "/sys/class/gpio/gpio150/value"
#define HDMI_STATE_PATH "/sys/class/drm/card0-HDMI-A-1/status"

typedef struct SettingsV7 {
	int version; // future proofing
	int brightness;
	int colortemperature;
	int headphones;
	int speaker;
	int mute;
	int contrast;
	int saturation;
	int exposure;
	int mutedbrightness;
	int mutedcolortemperature;
	int mutedcontrast;
	int mutedsaturation;
	int hdmi;
	int mutedexposure;
	int unused[2]; // for future use
	// NOTE: doesn't really need to be persisted but still needs to be shared
	int jack; 
} SettingsV7;

#define SETTINGS_VERSION 7
typedef SettingsV7 Settings;

static Settings DefaultSettings = {
	.version = SETTINGS_VERSION,
	.brightness = SETTINGS_DEFAULT_BRIGHTNESS,
	.colortemperature = SETTINGS_DEFAULT_COLORTEMP,
	.headphones = SETTINGS_DEFAULT_HEADPHONE_VOLUME,
	.speaker = SETTINGS_DEFAULT_VOLUME,
	.mute = 0,
	.contrast = SETTINGS_DEFAULT_CONTRAST,
	.saturation = SETTINGS_DEFAULT_SATURATION,
	.exposure = SETTINGS_DEFAULT_EXPOSURE,
	.mutedbrightness = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.mutedcolortemperature = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.mutedcontrast = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.mutedsaturation = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.mutedexposure = SETTINGS_DEFAULT_MUTE_NO_CHANGE,
	.jack = 0,
};
static Settings* settings;

#define SHM_KEY "/SharedSettings"
static char SettingsPath[256];
static int shm_fd = -1;
static int is_host = 0;
static int shm_size = sizeof(Settings);

int getInt(char* path) {
	int i = 0;
	FILE *file = fopen(path, "r");
	if (file!=NULL) {
		fscanf(file, "%i", &i);
		fclose(file);
	}
	return i;
}
void getFile(char* path, char* buffer, size_t buffer_size) {
	FILE *file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		size_t size = ftell(file);
		if (size>buffer_size-1) size = buffer_size - 1;
		rewind(file);
		fread(buffer, sizeof(char), size, file);
		fclose(file);
		buffer[size] = '\0';
	}
}
void putFile(char* path, char* contents) {
	FILE* file = fopen(path, "w");
	if (file) {
		fputs(contents, file);
		fclose(file);
	}
}
void putInt(char* path, int value) {
	char buffer[8];
	sprintf(buffer, "%d", value);
	putFile(path, buffer);
}

int exactMatch(char* str1, char* str2) {
	int len1 = strlen(str1);
	if (len1!=strlen(str2)) return 0;
	return (strncmp(str1,str2,len1)==0);
}



static int JACK_enabled(void) {
	return !getInt(JACK_STATE_PATH);
}
static int HDMI_enabled(void) {
	char value[64];
	getFile(HDMI_STATE_PATH, value, 64);
	return exactMatch(value, "connected\n");
}

static int is_brick = 0;

void InitSettings(void) {	
	sprintf(SettingsPath, "%s/msettings.bin", getenv("USERDATA_PATH"));
	
	shm_fd = shm_open(SHM_KEY, O_RDWR | O_CREAT | O_EXCL, 0644); // see if it exists
	if (shm_fd == -1 && errno == EEXIST) { // already exists
		puts("Settings client");
		shm_fd = shm_open(SHM_KEY, O_RDWR, 0644);
		settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	}
	else { // host
		puts("Settings host"); // should always be keymon
		is_host = 1;
		// we created it so set initial size and populate
		ftruncate(shm_fd, shm_size);
		settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		
		int fd = open(SettingsPath, O_RDONLY);
		if (fd>=0) {
			read(fd, settings, shm_size);
			// TODO: use settings->version for future proofing?
			close(fd);
		}
		else {
			// load defaults
			memcpy(settings, &DefaultSettings, shm_size);
		}
		
		// these shouldn't be persisted
		// settings->jack = 0;
		// settings->hdmi = 0;
	}
	int jack = JACK_enabled();
	int hdmi = HDMI_enabled();
	printf("brightness: %i (hdmi: %i)\nspeaker: %i (jack: %i)\n", settings->brightness, hdmi, settings->speaker, jack); fflush(stdout);
	
    // both of these set volume
	SetJack(jack);
	SetHDMI(hdmi);
	
	char cmd[256];
	sprintf(cmd, "amixer sset 'Playback Path' '%s' > /dev/null 2>&1", GetJack() ? "HP" : "SPK");
	system(cmd);
	
	SetVolume(GetVolume());
	SetBrightness(GetBrightness());
	// system("echo $(< " BRIGHTNESS_PATH ")");
}
void QuitSettings(void) {
	munmap(settings, shm_size);
	if (is_host) shm_unlink(SHM_KEY);
}
static inline void SaveSettings(void) {
	int fd = open(SettingsPath, O_CREAT|O_WRONLY, 0644);
	if (fd>=0) {
		write(fd, settings, shm_size);
		close(fd);
		sync();
	}
}

///////// Platform specific scaling

int scaleBrightness(int value) {
	int raw;
	if (is_brick) {
		switch (value) {
			case 0: raw=1; break; 		// 0
			case 1: raw=8; break; 		// 8
			case 2: raw=16; break; 		// 8
			case 3: raw=32; break; 		// 16
			case 4: raw=48; break;		// 16
			case 5: raw=72; break;		// 24
			case 6: raw=96; break;		// 24
			case 7: raw=128; break;		// 32
			case 8: raw=160; break;		// 32
			case 9: raw=192; break;		// 32
			case 10: raw=255; break;	// 64
		}
	}
	else {
		switch (value) {
			case 0: raw=4; break; 		//  0
			case 1: raw=6; break; 		//  2
			case 2: raw=10; break; 		//  4
			case 3: raw=16; break; 		//  6
			case 4: raw=32; break;		// 16
			case 5: raw=48; break;		// 16
			case 6: raw=64; break;		// 16
			case 7: raw=96; break;		// 32
			case 8: raw=128; break;		// 32
			case 9: raw=192; break;		// 64
			case 10: raw=255; break;	// 64
		}
	}
	return raw;
}
int scaleColortemp(int value) {
	int raw;
	
	switch (value) {
		case 0: raw=-200; break; 		// 8
		case 1: raw=-190; break; 		// 8
		case 2: raw=-180; break; 		// 16
		case 3: raw=-170; break;		// 16
		case 4: raw=-160; break;		// 24
		case 5: raw=-150; break;		// 24
		case 6: raw=-140; break;		// 32
		case 7: raw=-130; break;		// 32
		case 8: raw=-120; break;		// 32
		case 9: raw=-110; break;	// 64
		case 10: raw=-100; break; 		// 0
		case 11: raw=-90; break; 		// 8
		case 12: raw=-80; break; 		// 8
		case 13: raw=-70; break; 		// 16
		case 14: raw=-60; break;		// 16
		case 15: raw=-50; break;		// 24
		case 16: raw=-40; break;		// 24
		case 17: raw=-30; break;		// 32
		case 18: raw=-20; break;		// 32
		case 19: raw=-10; break;		// 32
		case 20: raw=0; break;	// 64
		case 21: raw=10; break; 		// 0
		case 22: raw=20; break; 		// 8
		case 23: raw=30; break; 		// 8
		case 24: raw=40; break; 		// 16
		case 25: raw=50; break;		// 16
		case 26: raw=60; break;		// 24
		case 27: raw=70; break;		// 24
		case 28: raw=80; break;		// 32
		case 29: raw=90; break;		// 32
		case 30: raw=100; break;		// 32
		case 31: raw=110; break;	// 64
		case 32: raw=120; break; 		// 0
		case 33: raw=130; break; 		// 8
		case 34: raw=140; break; 		// 8
		case 35: raw=150; break; 		// 16
		case 36: raw=160; break;		// 16
		case 37: raw=170; break;		// 24
		case 38: raw=180; break;		// 24
		case 39: raw=190; break;		// 32
		case 40: raw=200; break;		// 32
	}
	return raw;
}
int scaleContrast(int value) {
	int raw;
	
	switch (value) {
		// dont offer -5/ raw 0, looks like it might turn off the display completely?
		case -4: raw=10; break;
		case -3: raw=20; break;
		case -2: raw=30; break;
		case -1: raw=40; break;
		case 0: raw=50; break;
		case 1: raw=60; break;
		case 2: raw=70; break;
		case 3: raw=80; break;
		case 4: raw=90; break;
		case 5: raw=100; break;
	}
	return raw;
}
int scaleSaturation(int value) {
	int raw;
	
	switch (value) {
		case -5: raw=0; break;
		case -4: raw=10; break;
		case -3: raw=20; break;
		case -2: raw=30; break;
		case -1: raw=40; break;
		case 0: raw=50; break;
		case 1: raw=60; break;
		case 2: raw=70; break;
		case 3: raw=80; break;
		case 4: raw=90; break;
		case 5: raw=100; break;
	}
	return raw;
}
int scaleExposure(int value) {
	int raw;
	
	switch (value) {
		// stock OS also avoids setting anything lower, so we do the same here.
		case -4: raw=10; break;
		case -3: raw=20; break;
		case -2: raw=30; break;
		case -1: raw=40; break;
		case 0: raw=50; break;
		case 1: raw=60; break;
		case 2: raw=70; break;
		case 3: raw=80; break;
		case 4: raw=90; break;
		case 5: raw=100; break;
	}
	return raw;
}

int GetColortemp(void) { // 0-10
	return 5;
}
void SetColortemp(int value) { // 0-40
	return;
}

int GetBrightness(void) { // 0-10
	return settings->brightness;
}
void SetBrightness(int value) {
	if (settings->hdmi) return;
	
	int raw;
	switch (value) {
		case  0: raw =   8; break;
		case  1: raw =  12; break;
		case  2: raw =  16; break;
		case  3: raw =  24; break;
		case  4: raw =  32; break;
		case  5: raw =  48; break;
		case  6: raw =  64; break;
		case  7: raw =  96; break;
		case  8: raw = 128; break;
		case  9: raw = 192; break;
		case 10: raw = 255; break;
	}
	SetRawBrightness(raw);
	settings->brightness = value;
	SaveSettings();
}

int GetVolume(void) { // 0-20
	return settings->jack ? settings->headphones : settings->speaker;
}
void SetVolume(int value) {
	if (settings->hdmi) return;
	
	if (settings->jack) settings->headphones = value;
	else settings->speaker = value;
	
	int raw = value * 5;
	SetRawVolume(raw);
	SaveSettings();
}

#define DISP_LCD_SET_BRIGHTNESS  0x102
void SetRawBrightness(int val) { // 0 - 255
	if (settings->hdmi) return;
	
	printf("SetRawBrightness(%i)\n", val); fflush(stdout);
	putInt("/sys/class/backlight/backlight/brightness", val);
}
void SetRawColortemp(int val) { // 0 - 255
	// if (settings->hdmi) return;
	
	printf("SetRawColortemp(%i)\n", val); fflush(stdout);

	FILE *fd = fopen("/sys/class/disp/disp/attr/color_temperature", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}
void SetRawContrast(int val){
	// if (settings->hdmi) return;
	
	printf("SetRawContrast(%i)\n", val); fflush(stdout);

	FILE *fd = fopen("/sys/class/disp/disp/attr/enhance_contrast", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}
void SetRawSaturation(int val){
	// if (settings->hdmi) return;

	printf("SetRawSaturation(%i)\n", val); fflush(stdout);

	FILE *fd = fopen("/sys/class/disp/disp/attr/enhance_saturation", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}
void SetRawExposure(int val){
	// if (settings->hdmi) return;

	printf("SetRawExposure(%i)\n", val); fflush(stdout);

	FILE *fd = fopen("/sys/class/disp/disp/attr/enhance_bright", "w");
	if (fd) {
		fprintf(fd, "%i", val);
		fclose(fd);
	}
}
void SetRawVolume(int val) { // 0 - 100
	printf("SetRawVolume(%i)\n", val); fflush(stdout);
	
	system("amixer sset 'SPK' 1% > /dev/null 2>&1"); // ensure there is always a change
	if (settings->jack) {
		system("amixer sset 'Playback Path' 'HP' > /dev/null 2>&1");
		puts("headphones"); fflush(stdout);
	}
	else if (val==0) {
		system("amixer sset 'Playback Path' 'OFF' > /dev/null 2>&1"); // mute speaker (not headphone as that produces white noise)
		puts("mute"); fflush(stdout);
	}
	else {
		system("amixer sset 'Playback Path' 'SPK' > /dev/null 2>&1");
		puts("speaker"); fflush(stdout);
	}
	
	char cmd[256];
	sprintf(cmd, "amixer sset 'SPK' %i%% > /dev/null 2>&1", val);
	puts(cmd); fflush(stdout);
	system(cmd);
}

// monitored and set by thread in keymon?
int GetJack(void) {
	return settings->jack;
}
void SetJack(int value) {
	settings->jack = value;
	SetVolume(GetVolume());
}

int GetHDMI(void) {	return settings->hdmi; }
void SetHDMI(int value) { 
    settings->hdmi = value;
    if (value) SetRawVolume(100); // max
    else SetVolume(GetVolume()); // restore
}

int GetMute(void) { return 0; }
void SetMute(int value) {}

int GetContrast(void)
{
	return settings->contrast;
}
int GetSaturation(void)
{
	return settings->saturation;
}
int GetExposure(void)
{
	return settings->exposure;
}
int GetMutedBrightness(void)
{
	return settings->mutedbrightness;
}
int GetMutedColortemp(void)
{
	return settings->mutedcolortemperature;
}
int GetMutedContrast(void)
{
	return settings->mutedcontrast;
}
int GetMutedSaturation(void)
{
	return settings->mutedsaturation;
}
int GetMutedExposure(void)
{
	return settings->mutedexposure;
}

void SetContrast(int value)
{
	int raw = scaleContrast(value);
	SetRawContrast(raw);
	settings->contrast = value;
	SaveSettings();
}
void SetSaturation(int value)
{
	int raw = scaleSaturation(value);
	SetRawSaturation(raw);
	settings->saturation = value;
	SaveSettings();
}
void SetExposure(int value)
{
	int raw = scaleExposure(value);
	SetRawExposure(raw);
	settings->exposure = value;
	SaveSettings();
}

void SetMutedBrightness(int value)
{
	settings->mutedbrightness = value;
	SaveSettings();
}

void SetMutedColortemp(int value)
{
	settings->mutedcolortemperature = value;
	SaveSettings();
}

void SetMutedContrast(int value)
{
	settings->mutedcontrast = value;
	SaveSettings();
}

void SetMutedSaturation(int value)
{
	settings->mutedsaturation = value;
	SaveSettings();
}

void SetMutedExposure(int value)
{
	settings->mutedexposure = value;
	SaveSettings();
}