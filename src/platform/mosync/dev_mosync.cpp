// This file is part of SmallBASIC
//
// Copyright(C) 2012 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0 or later
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <MAUI/Screen.h>
#include <MAUI/Layout.h>
#include <MAUI/EditBox.h>

#include "MAHeaders.h"

#include "platform/mosync/controller.h"
#include "platform/mosync/utils.h"
#include "languages/messages.en.h"

using namespace MAUI;

extern Controller *controller;
clock_t lastEventTime;
dword eventsPerTick;
int penMode;

#define EVT_MAX_BURN_TIME (CLOCKS_PER_SEC / 4)
#define EVT_PAUSE_TIME 5
#define EVT_CHECK_EVERY ((50 * CLOCKS_PER_SEC) / 1000)
#define PEN_OFF   0             // pen mode disabled
#define PEN_ON    2             // pen mode active

void osd_sound(int frq, int dur, int vol, int bgplay) {

}

void osd_clear_sound_queue() {

}

void osd_beep(void) {

}

void osd_cls(void) {
  controller->output->clearScreen();
}

int osd_devinit(void) {
  dev_fgcolor = -DEFAULT_COLOR;
  dev_bgcolor = 0;
  os_graf_mx = controller->output->getWidth();
  os_graf_my = controller->output->getHeight();

  os_ver = 1;
  os_color = 1;
  os_color_depth = 16;
  setsysvar_str(SYSVAR_OSNAME, "MoSync");

  osd_cls();
  dev_clrkb();
  ui_reset();
  controller->setRunning();
  return 1;
}

int osd_devrestore(void) {
  ui_reset();
}

int osd_events(int wait_flag) {
  if (!wait_flag) {
    // pause when we have been called too frequently
    clock_t now = clock();
    if (now - lastEventTime <= EVT_CHECK_EVERY) {
      eventsPerTick += (now - lastEventTime);
      if (eventsPerTick >= EVT_MAX_BURN_TIME) {
        eventsPerTick = 0;
        wait_flag = 2;
      }
    }
    lastEventTime = now;
  }

  switch (wait_flag) {
  case 1:
    // wait for an event
    controller->processEvents(-1, -1);
    break;
  case 2:
    // pause
    maWait(EVT_PAUSE_TIME);
    break;
  default:
    // pump messages without pausing
    maWait(1);
    break;
  }

  return controller->isExit() ? -2 : 0;
}

int osd_getpen(int mode) {
  return 0;
}

long osd_getpixel(int x, int y) {
  return controller->output->getPixel(x, y);
}

int osd_getx(void) {
  return controller->output->getX();
}

int osd_gety(void) {
  return controller->output->getY();
}

void osd_line(int x1, int y1, int x2, int y2) {
  controller->output->drawLine(x1, y1, x2, y2);
}

void osd_rect(int x1, int y1, int x2, int y2, int fill) {
  if (fill) {
    controller->output->drawRectFilled(x1, y1, x2, y2);
  } else {
    controller->output->drawRect(x1, y1, x2, y2);
  }
}

void osd_refresh(void) {
  controller->output->refresh();
}

void osd_setcolor(long color) {
  controller->output->setColor(color);
}

void osd_setpenmode(int enable) {
  penMode = (enable ? PEN_ON : PEN_OFF);
}

void osd_setpixel(int x, int y) {
  controller->output->setPixel(x, y, dev_fgcolor);
}

void osd_settextcolor(long fg, long bg) {
  controller->output->setTextColor(fg, bg);
}

void osd_setxy(int x, int y) {
  controller->output->setXY(x, y);
}

int osd_textheight(const char *str) {
  return EXTENT_Y(maGetTextSize(str));
}

int osd_textwidth(const char *str) {
  return EXTENT_X(maGetTextSize(str));
}

void osd_write(const char *str) {
  controller->output->print(str);
}

char *dev_read(const char *fileName) {
  char *buffer = NULL;
  
  if (strcasecmp(MAIN_BAS_RES, fileName) == 0) {
    // load as resource
    int len = maGetDataSize(MAIN_BAS);
    buffer = (char *)mem_alloc(len + 1);
    maReadData(MAIN_BAS, buffer, 0, len);
    buffer[len] = '\0';
  } else if (strstr(fileName, "://")) {
    buffer = controller->readConnection(fileName);
  } else {
    // load from file system
    MAHandle handle = maFileOpen(fileName, MA_ACCESS_READ);
    if (maFileExists(handle)) {
      int len = maFileSize(handle);
      buffer = (char *)mem_alloc(len);
      maFileRead(handle, buffer, len);
    }
    maFileClose(handle);
  }

  if (buffer == NULL) {
    panic(MSG_CANT_OPEN_FILE, fileName);
  }

  return buffer;
}

void dev_image(int handle, int index,
               int x, int y, int sx, int sy, int w, int h) {
}

int dev_image_width(int handle, int index) {
  return 0;
}

int dev_image_height(int handle, int index) {
  return 0;
}

void dev_delay(dword ms) {
  controller->pause(ms);
}

char *dev_gets(char *dest, int maxSize) {
  if (controller->isRun()) {
    wchar_t *buffer = new wchar_t[maxSize + 1];
    if (maTextBox(L"INPUT:", L"", buffer, maxSize, 0) >=0) {
      for (int i = 0; i < maxSize; i++) {
        dest[i] = buffer[i];
      }
      dest[maxSize] = 0;
      osd_write(dest);
    } else {
      dest[0] = 0;
    }
    delete [] buffer;
  }
  return dest;
}

extern "C" int access(const char *path, int amode) {
  return 0;
}

extern "C" void chmod(const char *s, int mode) {
}

