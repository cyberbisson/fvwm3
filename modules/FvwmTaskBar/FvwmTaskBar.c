/* FvwmTaskBar Module for Fvwm.
 *
 * (Much reworked version of FvwmWinList)
 *  Copyright 1994,  Mike Finger (mfinger@mermaid.micro.umn.edu or
 *                               Mike_Finger@atk.com)
 *
 * Minor hack by TKP to enable autohide to work with pages (leaves 2 pixels
 * visible so that the 1 pixel panframes don't get in the way)
 *
 * Merciless hack by RBW for the Great Style Flag Rewrite - 04/20/1999.
 * This module slightly abused the old flags word, inventing new meanings for
 * certain bits. Therefore, I'm leaving all that infrastructure alone, only
 * changing the private flag word's name and ensuring that it contains a bit
 * for the ICONIFIED state,which is all the module cares about once the flags
 * are added to its Item struct. The Item field is renamed to tb_flags.
 * For possible future use, I'm adding a field to the struct to carry the *real*
 * flags, called, oddly enough, flags. This is necessary in order to use the
 * standard macros for testing and manipulating the bitfields. The real flags
 * are not presently used.
 *
 *
 * The author makes not guarantees or warantees, either express or
 * implied.  Feel free to use any code contained here for any purpose, as long
 * and this and any other applicable copyrights are kept intact.

 * The functions in this source file that are based on part of the FvwmIdent
 * module for Fvwm are noted by a small copyright atop that function, all others
 * are copyrighted by Mike Finger.  For those functions modified/used, here is
 *  the full, original copyright:
 *
 * Copyright 1994, Robert Nation and Nobutaka Suzuki.
 * No guarantees or warantees or anything
 * are provided or implied in any way whatsoever. Use this program at your
 * own risk. Permission to use this program for any purpose is given,
 * as long as the copyright is kept intact. */

/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdarg.h>

#include <unistd.h>
#include <ctype.h>

#ifdef HAVE_SYS_BSDTYPES_H
#include <sys/bsdtypes.h> /* Saul */
#endif /* Saul */

#include <stdlib.h>

#ifdef I18N_MB
#include <X11/Xlocale.h>
#endif

#include "libs/Module.h"
#include "libs/fvwmlib.h"  /* for pixmaps routines */
#include "libs/safemalloc.h"
#include "libs/fvwmsignal.h"
#include "libs/Colorset.h"

#include "FvwmTaskBar.h"
#include "ButtonArray.h"
#include "List.h"
#include "Colors.h"
#include "Mallocs.h"
#include "Goodies.h"
#include "Start.h"


#define GRAB_EVENTS (ButtonPressMask|ButtonReleaseMask|ButtonMotionMask|EnterWindowMask|LeaveWindowMask)
#define SomeButtonDown(a) ((a)&Button1Mask||(a)&Button2Mask||(a)&Button3Mask)

#define DEFAULT_CLICK1 "Iconify -1, Raise, Focus"
#define DEFAULT_CLICK2 "Iconify +1, Lower"
#define DEFAULT_CLICK3 "Nop"

#define gray_width  8
#define gray_height 8
unsigned char gray_bits[] = { 0xaa, 0x55, 0xaa, 0x55,
                              0xaa, 0x55, 0xaa, 0x55 };

/* File type information */
FILE  *console;
int   Fvwm_fd[2];
int   x_fd;

/* X related things */
Display *dpy;
Window  Root, win;
int     screen;
Pixel back;
Pixel fore;
static Pixel iconfore;
static Pixel iconback;
GC      icongraph = None;
GC      iconbackgraph = None;
GC      graph = None;
GC      shadow = None;
GC      hilite = None;
GC      blackgc = None;
GC      whitegc = None;
GC      checkered = None;
int     colorset = -1;
int     iconcolorset = -1;
XFontStruct *ButtonFont, *SelButtonFont;
#ifdef I18N_MB
XFontSet ButtonFontset, SelButtonFontset;
#endif
int fontheight;
static Atom wm_del_win;
Atom MwmAtom = None;

/* Module related information */
char *Module;
int  win_width    = 5,
     win_height   = 5,
     win_grav,
     win_x,
     win_y,
     win_border,
     button_width = DEFAULT_BTN_WIDTH,
     Clength,
     ButPressed   = -1,
     ButReleased  = -1,
     Checked      = 0,
     BelayHide    = False;

static volatile sig_atomic_t AlarmSet = NOT_SET;
static volatile sig_atomic_t tip_window_alarm = False;
static volatile sig_atomic_t hide_taskbar_alarm = False;


int UpdateInterval = 30;

ButtonArray buttons;
List windows;
List swallowed;

char *ClickAction[3] = { DEFAULT_CLICK1, DEFAULT_CLICK2, DEFAULT_CLICK3 },
     *EnterAction,
     *BackColor      = "white",
     *ForeColor      = "black",
     *IconBackColor  = "white",
     *IconForeColor  = "black",
     *geometry       = NULL,
     *font_string    = "fixed",
     *selfont_string = NULL;

int UseSkipList    = False,
    UseIconNames   = False,
    ShowTransients = False,
    adjust_flag    = False,
    AutoStick      = False,
    AutoHide       = False,
    AutoFocus      = False,
    HighlightFocus = False,
    DeskOnly       = False;

unsigned int ScreenWidth, ScreenHeight;

int NRows, RowHeight, Midline;

#define COUNT_LIMIT    10000
long DeskNumber;               /* Added by Balaji R */
int  First = 1;
int  Count = 0;

/* Imported from Goodies */
extern int stwin_width, goodies_width;
extern TipStruct Tip;

/* Imported from Start */
extern int StartButtonWidth, StartButtonHeight;
extern char *StartPopup;

char *ImagePath   = NULL;

static void ParseConfig( void );
static void ParseConfigLine(char *tline);
static void ShutMeDown(void);
static RETSIGTYPE TerminateHandler(int sig);
static RETSIGTYPE Alarm(int sig);
static void SetAlarm(int event);
static void ClearAlarm(void);
static int ErrorHandler(Display*, XErrorEvent*);
static Bool change_colorset(int cset);

/******************************************************************************
  Main - Setup the XConnection,request the window list and loop forever
    Based on main() from FvwmIdent:
      Copyright 1994, Robert Nation and Nobutaka Suzuki.
******************************************************************************/
int main(int argc, char **argv)
{
  const char *temp;
  char *s;

  /* Save the program name for error messages and config parsing */
  temp = argv[0];
  s=strrchr(argv[0], '/');
  if (s != NULL)
    temp = s + 1;

  /* Setup my name */
  Module = safemalloc(strlen(temp)+2);
  strcpy(Module,"*");
  strcat(Module, temp);
  Clength = strlen(Module);

#ifdef I18N_MB
  setlocale(LC_CTYPE, "");
#endif
  /* Open the console for messages */
  OpenConsole();

  if((argc != 6)&&(argc != 7)) {
    fprintf(stderr,"%s Version %s should only be executed by fvwm!\n",Module,
	    VERSION);
    ConsoleMessage("%s Version %s should only be executed by fvwm!\n",Module,
		   VERSION);
    exit(1);
  }

  /* setup fvwm pipes */
  Fvwm_fd[0] = atoi(argv[1]);
  Fvwm_fd[1] = atoi(argv[2]);

#ifdef HAVE_SIGACTION
  {
    struct sigaction  sigact;

    sigemptyset(&sigact.sa_mask);
#ifdef SA_RESTART
    sigact.sa_flags = SA_RESTART;
#else
    sigact.sa_flags = 0;
#endif
    sigact.sa_handler = Alarm;
    sigaction(SIGALRM, &sigact, NULL);

#ifdef SA_INTERRUPT
    sigact.sa_flags = SA_INTERRUPT;
#else
    sigact.sa_flags = 0;
#endif
    sigaddset(&sigact.sa_mask, SIGPIPE);
    sigaddset(&sigact.sa_mask, SIGTERM);
    sigaddset(&sigact.sa_mask, SIGINT);
    sigact.sa_handler = TerminateHandler;
    sigaction(SIGPIPE, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGINT,  &sigact, NULL);
  }
#else
#ifdef USE_BSD_SIGNALS
  fvwmSetSignalMask( sigmask(SIGTERM) | sigmask(SIGPIPE) | sigmask(SIGINT) );
#endif
  signal (SIGPIPE, TerminateHandler);
  signal (SIGTERM, TerminateHandler);
  signal (SIGINT,  TerminateHandler);
  signal (SIGALRM, Alarm);
#ifdef HAVE_SIGINTERRUPT
  siginterrupt(SIGPIPE, 1);
  siginterrupt(SIGTERM, 1);
  siginterrupt(SIGINT, 1);
#endif
#endif

  SetMessageMask(Fvwm_fd,M_ADD_WINDOW | M_CONFIGURE_WINDOW | M_DESTROY_WINDOW |
		 M_WINDOW_NAME | M_ICON_NAME | M_RES_NAME | M_DEICONIFY |
		 M_ICONIFY | M_END_WINDOWLIST | M_FOCUS_CHANGE |
		 M_CONFIG_INFO | M_END_CONFIG_INFO | M_NEW_DESK | M_SENDCONFIG
#ifdef MINI_ICONS
		 | M_MINI_ICON
#endif
		);

  /* Parse the config file */
  InitList(&swallowed);

  ParseConfig();

  /* Setup the XConnection */
  StartMeUp();
  XSetErrorHandler(ErrorHandler);

  StartButtonInit(RowHeight);

  /* init the array of buttons */
  InitArray(&buttons, StartButtonWidth + 4, 0,
                      win_width - stwin_width - 8 - StartButtonWidth -10,
                      RowHeight, button_width);
  InitList(&windows);

  /* Request a list of all windows,
   * wait for ConfigureWindow packets */
  SendFvwmPipe("Send_WindowList",0);

  /* tell fvwm we're running */
  SendFinishedStartupNotification(Fvwm_fd);

  /* Receive all messages from Fvwm */
  EndLessLoop();
#ifdef FVWM_DEBUG_MSGS
  if ( isTerminated )
  {
    ConsoleMessage("Received signal: exiting...\n");
  }
#endif
  return 0;
}


/******************************************************************************
  EndLessLoop -  Read and redraw until we get killed, blocking when can't read
******************************************************************************/
void EndLessLoop(void)
{
  fd_set readset;
  fd_set_size_t  fd_width;
  struct timeval tv;

  fd_width = x_fd;
  if (Fvwm_fd[1] > fd_width)
    fd_width = Fvwm_fd[1];
  ++fd_width;

  while ( !isTerminated )
  {
    FD_ZERO(&readset);
    FD_SET(Fvwm_fd[1], &readset);
    FD_SET(x_fd, &readset);

    tv.tv_sec  = UpdateInterval;
    tv.tv_usec = 0;

    XFlush(dpy);
    if ( fvwmSelect(fd_width, &readset, NULL, NULL, &tv) > 0 )
    {
      if (FD_ISSET(x_fd, &readset) || XPending(dpy))
        LoopOnEvents();

      if (FD_ISSET(Fvwm_fd[1], &readset))
        ReadFvwmPipe();
    }

    DrawGoodies();
  } /* while */
}


/******************************************************************************
  ReadFvwmPipe - Read a single message from the pipe from Fvwm
    Originally Loop() from FvwmIdent:
      Copyright 1994, Robert Nation and Nobutaka Suzuki.
******************************************************************************/
void ReadFvwmPipe(void)
{
    FvwmPacket* packet = ReadFvwmPacket(Fvwm_fd[1]);
    if ( packet == NULL )
	DeadPipe(0);
    else
	ProcessMessage( packet->type, packet->body );
}

/******************************************************************************
  ProcessMessage - Process the message coming from Fvwm
    Skeleton based on processmessage() from FvwmIdent:
      Copyright 1994, Robert Nation and Nobutaka Suzuki.
******************************************************************************/
void ProcessMessage(unsigned long type,unsigned long *body)
{
  int redraw=-1;
  int i;
  char *string;
  long Desk;
  Picture p;
  struct ConfigWinPacket  *cfgpacket;
  unsigned long tb_flags;

/*    memset(&p, 0, sizeof(Picture)); */

  switch(type)
  {
  case M_FOCUS_CHANGE:
    i = FindItem(&windows, body[0]);
    if (win != body[0])
    {
      /* This case is handled in LoopOnEvents() */
      if (ItemFlags(&windows, body[0]) & F_ICONIFIED)
	i = -1;
      RadioButton(&buttons, i, BUTTON_BRIGHT);
      ButPressed = i;
      ButReleased = -1;
      redraw = 0;
    }
    break;

  case M_ADD_WINDOW:
  case M_CONFIGURE_WINDOW:
    /* Matched only at startup when default border width and
       actual border width differ. Don't assign win_width here so
       Window redraw and rewarping gets handled by XEvent
       ConfigureNotify code. */
    cfgpacket = (ConfigWinPacket *) body;
    if (!ShowTransients && (IS_TRANSIENT(cfgpacket)))
      break;
    if (cfgpacket->w == win)
    {
      if (win_border != (int)cfgpacket->border_width)
      {
	win_x = win_border = (int)cfgpacket->border_width;

        if (win_y > Midline)
          win_y = ScreenHeight - (AutoHide ? 2 : win_height + win_border);
        else
          win_y = AutoHide ? 2 - win_height : win_border;

        XMoveResizeWindow(dpy, win, win_x, win_y,
                          ScreenWidth-(win_border<<1), win_height);
      }
      break;
    }

    if ((i=FindItem(&windows, cfgpacket->w)) != -1)
    {
      if (GetDeskNumber(&windows,i,&Desk) && DeskOnly)
      {
        if (DeskNumber != Desk && DeskNumber == cfgpacket->desk)
	{
          /* window moving to current desktop */
          AddButton(&buttons, ItemName(&windows,i),
                    GetItemPicture(&windows,i), BUTTON_UP, i,
		    !!(ItemIndexFlags(&windows, i) & F_ICONIFIED));
          redraw = 1;
        }
        if (DeskNumber != cfgpacket->desk && DeskNumber == Desk)
	{
          /* window moving to another desktop */
          RemoveButton(&buttons, i);
          redraw = 1;
        }
	/* NEED TO DETERMINE IF BODY[8] IS RIGHT HERE! */
	tb_flags = ItemFlags(&windows, cfgpacket->w);
	UpdateItemFlagsDesk(&windows, cfgpacket->w, tb_flags, cfgpacket->desk);
      }
    }
    else if (!(DO_SKIP_WINDOW_LIST(cfgpacket)) || !UseSkipList)
    {
      AddItem(&windows, cfgpacket->w,
	      IS_ICONIFIED(cfgpacket) ? F_ICONIFIED : 0,
	      cfgpacket, cfgpacket->desk, Count++);
      if (Count > COUNT_LIMIT)
	Count = 0;
      i = FindItem(&windows, cfgpacket->w);
    }
    if (i != -1)
    {
      Button *temp = find_n(&buttons, i);
      if (temp && IS_ICONIFIED(cfgpacket))
	temp->iconified = 1;
    }
    break;

  case M_DESTROY_WINDOW:
    /*      if ((i = DeleteItem(&windows, body[0])) == -1) */
    if ((i = FindItem(&windows, body[0])) == -1)
      break;
    if (FindItem(&swallowed, body[0]) != -1)
      break;

    if (GetDeskNumber(&windows, i, &Desk))
    {
      DeleteItem(&windows, body[0]);
      RemoveButton(&buttons, i);
      redraw = 1;
    }
    break;

#ifdef MINI_ICONS
  case M_MINI_ICON:
    if ((i = FindItem(&windows, body[0])) == -1) break;
    if (UpdateButton(&buttons, i, NULL, DONT_CARE) != -1) {
      p.picture = body[6];
      p.mask    = body[7];
      p.width   = body[3];
      p.height  = body[4];
      p.depth   = body[5];
      UpdateButtonPicture(&buttons, i, &p);
      redraw = 0;
    }
    break;
#endif

  case M_WINDOW_NAME:
  case M_ICON_NAME:
    if ((type == M_ICON_NAME   && !UseIconNames) ||
	(type == M_WINDOW_NAME &&  UseIconNames)) break;
    string = (char *) &body[3];
    if ((i = FindNameItem(&swallowed, string)) != -1) {
      if (ItemIndexFlags(&swallowed, i) == F_NOT_SWALLOWED) {
	Swallow(body);
	break;
      }
    }
    if ((i = UpdateItemName(&windows, body[0], (char *)&body[3])) == -1)
      break;
    if (UpdateButton(&buttons, i, string, DONT_CARE) == -1)
    {
      if (GetDeskNumber(&windows, i, &Desk) == 0)
	break;
      if (!DeskOnly || Desk == DeskNumber)
      {
	AddButton(&buttons, string, NULL, BUTTON_UP, i,
		  !!(ItemIndexFlags(&windows, i) & F_ICONIFIED));
	redraw = 1;
      }
    }
    else
      redraw = 0;
    break;

  case M_DEICONIFY:
  case M_ICONIFY:
    if ((i = FindItem(&windows, body[0])) == -1)
      break;
    tb_flags = ItemFlags(&windows, body[0]);
    if (type == M_DEICONIFY && !(tb_flags & F_ICONIFIED))
      break;
    if (type == M_ICONIFY   &&   tb_flags & F_ICONIFIED)
      break;
    tb_flags ^= F_ICONIFIED;
    UpdateItemFlags(&windows, body[0], tb_flags);
    {
      Button *temp = find_n(&buttons, i);
      if (temp) {
	temp->needsupdate = 1;
	temp->iconified = (tb_flags & F_ICONIFIED) ? 1 : 0;
	DrawButtonArray(&buttons, 0);
      }
    }
    if (type == M_ICONIFY && i == ButReleased) {
      RadioButton(&buttons, -1, BUTTON_UP);
      ButReleased = ButPressed = -1;
      redraw = 0;
    }
    break;

  case M_END_WINDOWLIST:
    XMapRaised(dpy, win);
    break;

  case M_NEW_DESK:
    DeskNumber = body[0];
    if (!First && DeskOnly)
      redraw_buttons();
    else
      First = 0;
    break;

  case M_NEW_PAGE:
    break;

  case M_CONFIG_INFO:
    {
      char *tline;
      int cset;

      tline = (char*)&(body[3]);
      if (strncasecmp(tline, "Colorset", 8) == 0)
      {
	cset = LoadColorset(tline + 8);
	if (change_colorset(cset))
	{
	  redraw = 1;
	}
      }
    }
    break;
  } /* switch */

  if (redraw >= 0)
    RedrawWindow(redraw);
}


/* #define DEBUG_DESKONLY */

void redraw_buttons()
{
  Item *item;

#ifdef DEBUG_DESKONLY
  /* print buttons.count, buttons.tw here */
  fprintf(stderr,"Beginning of redraw_buttons()\n");
  fprintf(stderr,"-----------------------------\n\n");
  fprintf(stderr,"buttons.count = %i \t ",buttons.count);
  fprintf(stderr,"buttons.tw = %i\n",buttons.tw);

  fprintf(stderr,"Starting to remove old buttons...\n");
#endif
  FreeAllButtons(&buttons);

#ifdef DEBUG_DESKONLY
  /* print buttons.count, buttons.tw here */
  fprintf(stderr,"\nEnd of removing old buttons...\n\n");
  fprintf(stderr,"Starting to add new desk buttons...\n");
#endif

  for (item=windows.head; item; item=item->next)
  {
    if (DeskNumber == item->Desk || IS_STICKY(item))
    {
      AddButton(&buttons, item->name, &(item->p), BUTTON_UP, item->count,
		!!(item->tb_flags & F_ICONIFIED));
#ifdef DEBUG_DESKONLY
      /* print buttons.count, buttons.tw here */
      fprintf(stderr,"buttons.count = %i \t ",buttons.count);
      fprintf(stderr,"buttons.tw = %i\n",buttons.tw);
#endif
    }
  }

#ifdef DEBUG_DESKONLY
  /* print buttons.count, buttons.tw here */
  fprintf(stderr,"\nBefore RedrawWindow()...\n");
#endif

  RedrawWindow(1);

#ifdef DEBUG_DESKONLY
  fprintf(stderr,"\nAfter RedrawWindow()...\n");
  fprintf(stderr,"buttons.count = %i \t ",buttons.count);
  fprintf(stderr,"buttons.tw = %i\n",buttons.tw);
#endif
}


/******************************************************************************
  SendFvwmPipe - Send a message back to fvwm
    Based on SendInfo() from FvwmIdent:
      Copyright 1994, Robert Nation and Nobutaka Suzuki.
******************************************************************************/
void SendFvwmPipe(char *message, unsigned long window)
{
  int  w;
  char *hold, *temp, *temp_msg;

  hold = message;

  while(1) {
    temp = strchr(hold, ',');
    if (temp != NULL) {
      temp_msg = safemalloc(temp-hold+1);
      strncpy(temp_msg, hold, (temp-hold));
      temp_msg[(temp-hold)] = '\0';
      hold = temp+1;
    } else temp_msg = hold;

    write(Fvwm_fd[0], &window, sizeof(unsigned long));

    w=strlen(temp_msg);
    write(Fvwm_fd[0], &w, sizeof(int));
    write(Fvwm_fd[0], temp_msg, w);

    /* keep going */
    w = 1;
    write(Fvwm_fd[0], &w, sizeof(int));

    if(temp_msg == hold)
      break;
    free(temp_msg);
  }
}

/***********************************************************************
  Detected a broken pipe - time to exit
    Based on DeadPipe() from FvwmIdent:
      Copyright 1994, Robert Nation and Nobutaka Suzuki.
 **********************************************************************/
void DeadPipe(int nonsense)
{
  exit(1);
}

/**********************************************************************
  Reentrant signal handler that will tell the application to quit.
    It is not safe to just call "exit()" here; instead we will
    just set a flag that will break the event-loop when the
    handler returns.
 **********************************************************************/
static RETSIGTYPE
TerminateHandler(int sig)
{
  fvwmSetTerminate(sig);
}

/******************************************************************************
  WaitForExpose - Used to wait for expose event so we don't draw too early
******************************************************************************/
void WaitForExpose(void)
{
  XEvent Event;

  while(1) {
    XNextEvent(dpy, &Event);
    if (Event.type == Expose) {
      if (Event.xexpose.count == 0) break;
    }
  }
}

/******************************************************************************
  RedrawWindow - Update the needed lines and erase any old ones
******************************************************************************/
void RedrawWindow(int force)
{
  if (Tip.open)
  {
    RedrawTipWindow();

    /* 98-11-21 13:45, Mohsin_Ahmed, mailto:mosh@sasi.com. */
    if( Tip.type >= 0 && AutoFocus )
    {
      SendFvwmPipe( "Iconify off, Raise, Focus",
		    ItemID( &windows, Tip.type ) );
    }
  }

  if (force)
  {
    XClearArea (dpy, win, 0, 0, win_width, win_height, False);
    DrawGoodies();
  }
  DrawButtonArray(&buttons, force);
  StartButtonDraw(force);
  if (XQLength(dpy) && !force)
    LoopOnEvents();
}

/******************************************************************************
  ConsoleMessage - Print a message on the console.  Works like printf.
******************************************************************************/
void ConsoleMessage(char *fmt, ...)
{
#ifndef NO_CONSOLE
  va_list args;
  FILE *filep;

  if (console == NULL)
    filep = stderr;
  else
    filep = console;
  va_start(args, fmt);
  vfprintf(filep, fmt, args);
  va_end(args);
  fflush(console);
#endif
}

/******************************************************************************
  OpenConsole - Open the console as a way of sending messages
******************************************************************************/
int OpenConsole(void)
{
#ifndef NO_CONSOLE
  if ((console = fopen("/dev/console","w")) == NULL)
  {
    fprintf(stderr, "%s: cannot open console\n", Module);
    return 0;
  }
#endif
  return 1;
}

static char *configopts[] =
{
  "imagepath",
  "colorset",
  NULL
};

static char *moduleopts[] =
{
  "Font",
  "SelFont",
  "Geometry",
  "Fore",
  "Back",
  "Colorset",
  "IconFore",
  "IconBack",
  "IconColorset",
  "Action",
  "UseSkipList",
  "AutoFocus",
  "AutoStick",
  "AutoHide",
  "UseIconNames",
  "ShowTransients",
  "DeskOnly",
  "UpdateInterval",
  "HighlightFocus",
  "SwallowModule",
  "Swallow",
  "ButtonWidth",
  NULL
};

void ParseConfig(void)
{
  char *buf;

  InitGetConfigLine(Fvwm_fd,Module);
  while (GetConfigLine(Fvwm_fd,&buf), buf != NULL)
  {
    ParseConfigLine(buf);
  } /* end config lines */
} /* end function */

static void ParseConfigLine(char *tline)
{
  char *str;
  char *rest;
  int i, j;
  int index;

  while (isspace((unsigned char)*tline))
    tline++;

  if (strncasecmp(tline, Module, Clength) != 0)
  {
    /* Non module spcific option */
    index = GetTokenIndex(tline, configopts, -1, &rest);
    while (*rest && *rest != '\n' && isspace(*rest))
      rest++;
    switch(index)
    {
    case 0: /* imagepath */
      CopyString(&ImagePath, rest);
      break;
    case 1: /* colorset */
      LoadColorset(rest);
      break;
    default:
      /* unknown option */
      break;
    }
  } /* if options */
  else
  {
    /* option beginning with '*ModuleName' */
    rest = tline + Clength;
    index = GetTokenIndex(rest, moduleopts, -1, &rest);
    while (*rest && *rest != '\n' && isspace(*rest))
      rest++;
    switch(index)
    {
    case 0: /* Font */
      CopyString(&font_string, rest);
      break;
    case 1: /* Selfont */
      CopyString(&selfont_string, rest);
      break;
    case 2: /* Geometry */
      while (isspace((unsigned char)*rest) && *rest != '\n' && *rest != 0)
	rest++;
      if (*rest != 0)
	rest[strlen(rest) - 1] = 0;
      UpdateString(&geometry, rest);
      break;
    case 3: /* Fore */
      CopyString(&ForeColor, rest);
      colorset = -1;
      break;
    case 4: /* Back */
      CopyString(&BackColor, rest);
      colorset = -1;
      break;
    case 5: /* Colorset */
      colorset = -1;
      colorset = atoi(rest);
      AllocColorset(colorset);
      break;
    case 6: /* IconFore */
      CopyString(&IconForeColor, rest);
      iconcolorset = -1;
      break;
    case 7: /* IconBack */
      CopyString(&IconBackColor, rest);
      iconcolorset = -1;
      break;
    case 8: /* IconColorset */
      iconcolorset = -1;
      iconcolorset = atoi(rest);
      AllocColorset(iconcolorset);
      break;
    case 9: /* Action */
      LinkAction(rest);
      break;
    case 10: /* UseSkipList */
      UseSkipList=True;
      break;
    case 11: /* AutoFocus */
      AutoFocus=True;
      break;
    case 12: /* AutoStick */
      AutoStick=True;
      break;
    case 13: /* AutoHide */
      AutoHide=True;
      AutoStick=True;
      break;
    case 14: /* UseIconNames */
      UseIconNames=True;
      break;
    case 15: /* ShowTransients */
      ShowTransients=True;
      break;
    case 16: /* DeskOnly */
      DeskOnly=True;
      break;
    case 17: /* UpdateInterval */
      UpdateInterval = atoi(rest);
      break;
    case 18: /* HighlightFocus */
      HighlightFocus=True;
      break;
    case 19: /* SwallowModule */
      {
	/* tell fvwm to launch the module for us */
	str = safemalloc(strlen(rest) + 8);
	sprintf(str, "Module %s", rest);
	ConsoleMessage("Trying to: %s", str);
	SendFvwmPipe(str, 0);

	/* Remember the anticipated window's name for swallowing */
	i = 4;
	while(str[i] != 0 && str[i] != '"')
	i++;
	j = ++i;
	while(str[i] != 0 && str[i] != '"')
	  i++;
	if (i > j)
	{
	  str[i] = 0;
	  ConsoleMessage("Looking for window: [%s]\n", &str[j]);
	  AddItemName(&swallowed, &str[j], F_NOT_SWALLOWED);
	}
	free(str);
      } /* swallowmodule */
      break;
    case 20: /* Swallow */
      {
	/* tell fvwm to Exec the process for us */
	str = safemalloc(strlen(rest) + 6);
	sprintf(str, "Exec %s", rest);
	ConsoleMessage("Trying to: %s", str);
	SendFvwmPipe(str, 0);

	/* Remember the anticipated window's name for swallowing */
	i = 4;
	while(str[i] != 0 && str[i] != '"')
	  i++;
	j = ++i;
	while( str[i] != 0 && str[i] != '"')
	  i++;
	if (i > j)
	{
	  str[i] = 0;
	  ConsoleMessage("Looking for window: [%s]\n", &str[j]);
	  AddItemName(&swallowed, &str[j], F_NOT_SWALLOWED);
	}
	free(str);
      } /* swallow */
      break;
    case 21: /* ButtonWidth */
      button_width = atoi(rest);
      break;
    default:
      if (!GoodiesParseConfig(tline) &&
	  !StartButtonParseConfig(tline))
      {
	fprintf(stderr,"%s: unknown configuration option %s", Module, tline);
      }
      break;
    } /* switch */
  } /* else options */
}

/******************************************************************************
  Swallow a process window
******************************************************************************/
void Swallow(unsigned long *body)
{
  XSizeHints hints;
  long supplied;
  int h,w;

  /* Swallow the window */
  XUnmapWindow(dpy, body[0]);

  if (!XGetWMNormalHints (dpy, (Window)body[0], &hints, &supplied))
    hints.flags = 0;
  h = win_height - 10; w = 80;
  ConstrainSize(&hints, &w, &h);
  XResizeWindow(dpy,(Window)body[0], w, h);

  stwin_width += w + 2;

  XReparentWindow(dpy,(Window)body[0], win,
		  win_width - stwin_width + goodies_width + 2, 5);
  goodies_width += w + 2;

  XMapWindow(dpy,body[0]);
  XSelectInput(dpy,(Window)body[0],
	       PropertyChangeMask|StructureNotifyMask);


  /* Do not swallow it next time */
  UpdateNameItem(&swallowed, (char*)&body[3], body[0], F_SWALLOWED);
  ConsoleMessage("-> Swallowed: %s\n",(char*)&body[3]);
  RedrawWindow(1);
}

/******************************************************************************
  Alarm - Handle a SIGALRM - used to implement timeout events
******************************************************************************/
static RETSIGTYPE
Alarm(int nonsense)
{
  XEvent event;
  Bool trigger_event = False;

  switch(AlarmSet)
  {
  case SHOW_TIP:
    if (!tip_window_alarm)
    {
      tip_window_alarm = True;
      trigger_event = True;
    }
    break;

  case HIDE_TASK_BAR:
    if (!hide_taskbar_alarm)
    {
      hide_taskbar_alarm = True;
      trigger_event = True;
    }
    break;
  }

  if (trigger_event)
  {
    event.xmotion.x = -1;
    event.xmotion.y = -1;
    event.xany.type = MotionNotify;
    XSendEvent(dpy, win, False, EnterWindowMask, &event);
  }
  AlarmSet = NOT_SET;
#if !defined(HAVE_SIGACTION) && !defined(USE_BSD_SIGNALS)
  signal (SIGALRM, Alarm);
#endif
}

/******************************************************************************
  CheckForTip - determine when to popup the tip window
******************************************************************************/
void CheckForTip(int x, int y)
{
  int  num, bx, by, trunc;
  char *name;

  if (MouseInStartButton(x, y)) {
    if (Tip.type != START_TIP) PopupTipWindow(3, 0, "Click here to start");
    Tip.type = START_TIP;
  }
  else if (MouseInMail(x, y)) {
    if (Tip.type != MAIL_TIP) CreateMailTipWindow();
    Tip.type = MAIL_TIP;
  }
  else if (MouseInClock(x, y)) {
    if (Tip.type != DATE_TIP) CreateDateWindow();
    Tip.type = DATE_TIP;
  }
  else {
    num = LocateButton(&buttons, x, y, &bx, &by, &name, &trunc);
    if (num != -1) {
      if ((Tip.type != num)  ||
          (Tip.text == NULL) || (!trunc && (strcmp(name, Tip.text) != 0)))
        PopupTipWindow(bx+3, by, name);
      Tip.type = num;
    } else {
      Tip.type = NO_TIP;
    }
  }

  if (Tip.type != NO_TIP) {
    if (AlarmSet != SHOW_TIP && !Tip.open)
      SetAlarm(SHOW_TIP);
  }
  else {
    ClearAlarm();
    if (Tip.open) ShowTipWindow(0);
  }
}


/******************************************************************************
  LoopOnEvents - Process all the X events we get
******************************************************************************/
void LoopOnEvents(void)
{
  int  num = 0;
  char tmp[100];
  XEvent Event;
  XEvent Event2;
  int x, y, redraw;
  static unsigned long lasttime = 0L;

  while(XPending(dpy)) {
    redraw = -1;
    XNextEvent(dpy, &Event);
    if (Event.xany.type == ConfigureNotify)
    {
      /* Purge all but the last configure events */
      while(XCheckTypedEvent(dpy, ConfigureNotify, &Event))
	;
    }

    switch(Event.type)
    {
      case ButtonRelease:
        num = WhichButton(&buttons, Event.xbutton.x, Event.xbutton.y);
        if (num == -1) {
	  if (MouseInStartButton(Event.xbutton.x, Event.xbutton.y))
	    StartButtonUpdate(NULL, BUTTON_UP);
	} else {
          ButReleased = ButPressed; /* Avoid race fvwm pipe */
          BelayHide = True; /* Don't AutoHide when function ends */
          SendFvwmPipe(ClickAction[Event.xbutton.button-1],
                       ItemID(&windows, num));
        }

        if (MouseInStartButton(Event.xbutton.x, Event.xbutton.y)) {
          StartButtonUpdate(NULL, BUTTON_UP);
          redraw = 0;
          usleep(50000);
        }

        if (HighlightFocus) {
          if (num == ButPressed)
	    RadioButton(&buttons, num, BUTTON_DOWN);
          if (num != -1)
	    SendFvwmPipe("Focus 0", ItemID(&windows, num));
        }
        ButPressed = -1;
	redraw = 0;
        break;

      case ButtonPress:
        RadioButton(&buttons, -1, BUTTON_UP); /* no windows focused anymore */
	if (MouseInStartButton(Event.xbutton.x, Event.xbutton.y)) {
	  StartButtonUpdate(NULL, BUTTON_DOWN);
          x = win_x;
          if (win_y < Midline) {
            /* bar in top half of the screen */
            y = win_y + RowHeight;
          } else {
            /* bar in bottom of the screen */
            y = win_y - ScreenHeight;
          }
          sprintf(tmp,"Popup %s %d %d", StartPopup, x, y);
          SendFvwmPipe(tmp, ItemID(&windows, num));
        } else {
	  StartButtonUpdate(NULL, BUTTON_UP);
	  if (MouseInMail(Event.xbutton.x, Event.xbutton.y)) {
	    HandleMailClick(Event);
	  } else {
	    num = WhichButton(&buttons, Event.xbutton.x, Event.xbutton.y);
	    UpdateButton(&buttons, num, NULL, (ButPressed == num) ?
			 BUTTON_BRIGHT : BUTTON_DOWN);

    /*	    UpdateButton(&buttons, num, NULL, BUTTON_DOWN);*/
	    ButPressed = num;
	}
/*      else { / * Move taskbar * /

          XUngrabPointer(dpy, CurrentTime);
          SendFvwmPipe("Move", win);

        }
*/
	}
        redraw = 0;
        break;

      case Expose:
        if (Event.xexpose.count == 0)
	{
          if (Event.xexpose.window == Tip.win)
            redraw = 0;
          else
	    redraw = 1;
	}
        break;

      case ClientMessage:
        if ((Event.xclient.format==32) && (Event.xclient.data.l[0]==wm_del_win))
          exit(0);
        break;

      case EnterNotify:
        if (AutoHide)
	  RevealTaskBar();

        if (Event.xcrossing.mode != NotifyNormal)
	  break;
        num = WhichButton(&buttons, Event.xcrossing.x, Event.xcrossing.y);
        if (!HighlightFocus) {
          if (SomeButtonDown(Event.xcrossing.state)) {
            if (num != -1) {
              RadioButton(&buttons, num, BUTTON_DOWN);
              ButPressed = num;
              redraw = 0;
            } else {
              ButPressed = -1;
            }
          }
        } else {
          if (num != -1 && num != ButPressed)
            SendFvwmPipe("Focus 0", ItemID(&windows, num));
        }

        CheckForTip(Event.xmotion.x, Event.xmotion.y);
        break;

      case LeaveNotify:
        ClearAlarm();
        if (Tip.open)
	  ShowTipWindow(0);

        if (Event.xcrossing.mode != NotifyNormal)
	  break;

        if (AutoHide)
	  SetAlarm(HIDE_TASK_BAR);

        if (!HighlightFocus) {
          if (SomeButtonDown(Event.xcrossing.state)) {
            if (ButPressed != -1) {
              RadioButton(&buttons, -1, BUTTON_UP);
              ButPressed = -1;
              redraw = 0;
            }
          } else {
            if (ButReleased != -1) {
              RadioButton(&buttons, -1, BUTTON_UP);
              ButReleased = -1;
              redraw = 0;
            }
          }
        }
        break;

      case MotionNotify:
	if (Event.xmotion.x < 0 && Event.xmotion.y < 0)
	{
	  /* This condition means that the event was triggered by an Alarm */
	  if (hide_taskbar_alarm)
	  {
	    hide_taskbar_alarm = False;
	    HideTaskBar();
	  }
	  else if (tip_window_alarm)
	  {
	    tip_window_alarm = False;
	    ShowTipWindow(1);
	  }
	  break;
	}
	if (MouseInStartButton(Event.xmotion.x, Event.xbutton.y)) {
	  if (SomeButtonDown(Event.xmotion.state))
	    redraw = StartButtonUpdate(NULL, BUTTON_DOWN) ? 0 : -1;
	  break;
	}
	redraw = StartButtonUpdate(NULL, BUTTON_UP) ? 0 : -1;
        num = WhichButton(&buttons, Event.xmotion.x, Event.xmotion.y);
        if (!HighlightFocus) {
          if (SomeButtonDown(Event.xmotion.state) && num != ButPressed) {
            if (num != -1) {
              RadioButton(&buttons, num, BUTTON_DOWN);
              ButPressed = num;
            } else {
              RadioButton(&buttons, num, BUTTON_UP);
              ButPressed = -1;
            }
            redraw = 0;
          }
        } else if (num != -1 && num != ButPressed)
	  SendFvwmPipe("Focus 0", ItemID(&windows, num));

        CheckForTip(Event.xmotion.x, Event.xmotion.y);
        break;

      case ConfigureNotify:
	memcpy(&Event2, &Event, sizeof(Event));
	/* eat up excess ConfigureNotify events. */
	while (XCheckTypedWindowEvent(dpy, win, ConfigureNotify, &Event2))
	{
	  memcpy(&Event, &Event2, sizeof(Event));
	}
        if ((Event.xconfigure.width != win_width ||
	     Event.xconfigure.height != win_height)) {
	  AdjustWindow(Event.xconfigure.width, Event.xconfigure.height);
          if (AutoStick)
	    WarpTaskBar(win_y);
	  redraw = 1;
        }
        else if (Event.xconfigure.x != win_x || Event.xconfigure.y != win_y)
	{
          if (AutoStick)
	  {
            WarpTaskBar(Event.xconfigure.y);
          }
	  else
	  {
            win_x = Event.xconfigure.x;
            win_y = Event.xconfigure.y;
          }
        }
        break;
    }

    if (redraw >= 0)
      RedrawWindow(redraw);

    if (Event.xkey.time - lasttime > UpdateInterval*1000L) {
      DrawGoodies();
      lasttime = Event.xkey.time;
    }
  }

}

/***********************************
  AdjustWindow - Resize the window
  **********************************/
void AdjustWindow(int width, int height)
{
  NRows = (height+2)/RowHeight;
  win_height = height;
  win_width = width;
  ArrangeButtonArray(&buttons);
  change_colorset(nColorsets);
}

/******************************************************************************
  makename - Based on the flags return me '(name)' or 'name'
******************************************************************************/
char *makename(const char *string,long flags)
{
  char *ptr;

  ptr=safemalloc(strlen(string)+3);
  *ptr = '\0';
  if (flags&F_ICONIFIED) *ptr = '(';
  strcat(ptr,string);
  if (flags&F_ICONIFIED) strcat(ptr,")");
  return ptr;
}

/******************************************************************************
  LinkAction - Link an response to a users action
******************************************************************************/
void LinkAction(const char *string)
{
  const char *temp=string;
  while(isspace((unsigned char)*temp)) temp++;
  if(strncasecmp(temp, "Click1", 6)==0)
    CopyString(&ClickAction[0],&temp[6]);
  else if(strncasecmp(temp, "Click2", 6)==0)
    CopyString(&ClickAction[1],&temp[6]);
  else if(strncasecmp(temp, "Click3", 6)==0)
    CopyString(&ClickAction[2],&temp[6]);
  else if(strncasecmp(temp, "Enter", 5)==0)
    CopyString(&EnterAction,&temp[5]);
}

static void CreateOrUpdateGCs(void)
{
   XGCValues gcval;
   unsigned long gcmask;
   Pixel pfore;
   Pixel pback;
   Pixel piconfore;
   Pixel piconback;
   Pixel philite;
   Pixel pshadow;

   if (colorset >= 0)
   {
     pfore = Colorset[colorset].fg;
     pback = Colorset[colorset].bg;
     philite = Colorset[colorset].hilite;
     pshadow = Colorset[colorset].shadow;
   }
   else
   {
     pfore = fore;
     pback = back;
     philite = GetHilite(back);
     if(Pdepth < 2)
       pshadow = GetShadow(fore);
     else
       pshadow = GetShadow(back);
   }
   if (iconcolorset >= 0)
   {
     piconfore = Colorset[iconcolorset].fg;
     piconback = Colorset[iconcolorset].bg;
   }
   else
   {
     piconfore = iconfore;
     piconback = iconback;
   }

   /* only the foreground changes for all GCs */
   gcval.background = pback;
   gcval.font = SelButtonFont->fid;
   gcval.graphics_exposures = False;

   gcmask = GCForeground | GCBackground | GCFont | GCGraphicsExposures;
   gcval.foreground = pfore;
   if (graph)
     XChangeGC(dpy,graph,gcmask,&gcval);
   else
     graph = XCreateGC(dpy,win,gcmask,&gcval);

   gcval.foreground = piconfore;
   if (iconbackgraph)
     XChangeGC(dpy,icongraph,gcmask,&gcval);
   else
     icongraph = XCreateGC(dpy,Root,gcmask,&gcval);

   gcval.foreground = piconback;
   if (iconbackgraph)
     XChangeGC(dpy,iconbackgraph,gcmask,&gcval);
   else
     iconbackgraph = XCreateGC(dpy,Root,gcmask,&gcval);

   gcmask = GCForeground | GCBackground | GCGraphicsExposures;
   gcval.foreground = pshadow;
   if (shadow)
     XChangeGC(dpy,shadow,gcmask,&gcval);
   else
     shadow = XCreateGC(dpy,win,gcmask,&gcval);

   gcval.foreground = philite;
   if (hilite)
     XChangeGC(dpy,hilite,gcmask,&gcval);
   else
     hilite = XCreateGC(dpy,win,gcmask,&gcval);

   gcval.foreground = WhitePixel(dpy, screen);;
   if (whitegc)
     XChangeGC(dpy,whitegc,gcmask,&gcval);
   else
     whitegc = XCreateGC(dpy,win,gcmask,&gcval);

   gcval.foreground = BlackPixel(dpy, screen);
   if (blackgc)
     XChangeGC(dpy,blackgc,gcmask,&gcval);
   else
     blackgc = XCreateGC(dpy,win,gcmask,&gcval);

   gcmask = GCForeground | GCBackground | GCTile |
            GCFillStyle  | GCGraphicsExposures;
   gcval.foreground = philite;
   gcval.fill_style = FillTiled;
   gcval.tile       = XCreatePixmapFromBitmapData(dpy, win, (char *)gray_bits,
						  gray_width, gray_height,
						  gcval.foreground,
						  gcval.background,Pdepth);
   if (checkered)
     XChangeGC(dpy, checkered, gcmask, &gcval);
   else
     checkered = XCreateGC(dpy, win, gcmask, &gcval);
}

static Bool change_colorset(int cset)
{
  Bool do_redraw = False;

  if (cset < 0)
    return False;
  if (cset == colorset || cset == iconcolorset || cset == nColorsets)
  {
    CreateOrUpdateGCs();
    if (cset == colorset || cset == nColorsets)
    {
      SetWindowBackground(
	dpy, win, win_width, win_height, &Colorset[colorset],
	Pdepth,	graph, True);
    }
    do_redraw = True;
  }
  do_redraw |= change_goody_colorset(cset);

  return do_redraw;
}


/******************************************************************************
  StartMeUp - Do X initialization things
******************************************************************************/
void StartMeUp(void)
{
   XSizeHints hints;
   int ret;
   XSetWindowAttributes attr;
#ifdef I18N_MB
  char **ml;
  int mc;
  char *ds;
  XFontStruct **fs_list;
#endif

   if (!(dpy = XOpenDisplay(""))) {
      fprintf(stderr,"%s: can't open display %s", Module,
	      XDisplayName(""));
      exit (1);
   }
   InitPictureCMap(dpy);
   AllocColorset(0);
   x_fd = XConnectionNumber(dpy);
   screen= DefaultScreen(dpy);
   Root = RootWindow(dpy, screen);

   ScreenWidth  = XDisplayWidth(dpy, screen);
   ScreenHeight = XDisplayHeight(dpy, screen);

   Midline = (int) (ScreenHeight >> 1);

   if (selfont_string == NULL) selfont_string = font_string;

#ifdef I18N_MB
   if ((ButtonFontset=XCreateFontSet(dpy,font_string,&ml,&mc,&ds)) == NULL) {
#ifdef STRICTLY_FIXED
     if ((ButtonFontset=XCreateFontSet(dpy,"fixed",&ml,&mc,&ds)) == NULL)
#else
     if ((ButtonFontset=XCreateFontSet(dpy,"-*-fixed-medium-r-normal-*-14-*-*-*-*-*-*-*",&ml,&mc,&ds)) == NULL)
#endif
       ConsoleMessage("Couldn't load fixed font. Exiting!\n");
       exit(1);
   }
   XFontsOfFontSet(ButtonFontset,&fs_list,&ml);
   ButtonFont = fs_list[0];
   if ((SelButtonFontset=XCreateFontSet(dpy,selfont_string,&ml,&mc,&ds)) == NULL) {
#ifdef STRICTLY_FIXED
     if ((SelButtonFontset=XCreateFontSet(dpy,"fixed",&ml,&mc,&ds)) == NULL)
#else
     if ((SelButtonFontset=XCreateFontSet(dpy,"-*-fixed-medium-r-normal-*-14-*-*-*-*-*-*-*",&ml,&mc,&ds)) == NULL)
#endif
       ConsoleMessage("Couldn't load fixed font. Exiting!\n");
       exit(1);
   }
   XFontsOfFontSet(SelButtonFontset,&fs_list,&ml);
   SelButtonFont = fs_list[0];
#else
   if ((ButtonFont = XLoadQueryFont(dpy, font_string)) == NULL) {
     if ((ButtonFont = XLoadQueryFont(dpy, "fixed")) == NULL) {
       ConsoleMessage("Couldn't load fixed font. Exiting!\n");
       exit(1);
     }
   }
   if ((SelButtonFont = XLoadQueryFont(dpy, selfont_string)) == NULL) {
     if ((SelButtonFont = XLoadQueryFont(dpy, "fixed")) == NULL) {
       ConsoleMessage("Couldn't load fixed font. Exiting!\n");
       exit(1);
     }
   }
#endif

   fontheight = SelButtonFont->ascent + SelButtonFont->descent;

   NRows = 1;
   RowHeight = fontheight + 8;

   win_border = 4; /* default border width */
   win_height = RowHeight;
   win_width = ScreenWidth - (win_border << 1);

   ret = XParseGeometry(geometry, &hints.x, &hints.y,
	 	        (unsigned int *)&hints.width,
		        (unsigned int *)&hints.height);

   if (ret & YNegative)
     hints.y = ScreenHeight - (AutoHide ? 1 : win_height + (win_border<<1));
   else
     hints.y = AutoHide ? 1 - win_height : 0;

   hints.flags=USPosition|PPosition|USSize|PSize|PResizeInc|
     PWinGravity|PMinSize|PMaxSize|PBaseSize;
   hints.x           = 0;
   hints.width       = win_width;
   hints.height      = RowHeight;
   hints.width_inc   = win_width;
   hints.height_inc  = RowHeight+2;
   hints.win_gravity = NorthWestGravity;
   hints.min_width   = win_width;
   hints.min_height  = RowHeight;
   hints.min_height  = win_height;
   hints.max_width   = win_width;
   hints.max_height  = RowHeight+7*(RowHeight+2) + 1;
   hints.base_width  = win_width;
   hints.base_height = RowHeight;

   win_x = hints.x;
   win_y = hints.y;

   if(Pdepth < 2) {
     back = WhitePixel(dpy, screen);
     fore = BlackPixel(dpy, screen);
     iconback = WhitePixel(dpy, screen);
     iconfore = BlackPixel(dpy, screen);
   } else {
     back = GetColor(BackColor);
     fore = GetColor(ForeColor);
     iconback = GetColor(IconBackColor);
     iconfore = GetColor(IconForeColor);
   }

   attr.background_pixel =
     (colorset >= 0) ? Colorset[colorset].bg : back;
   attr.border_pixel = 0;
   attr.colormap = Pcmap;
   win=XCreateWindow(dpy,Root,hints.x,hints.y,hints.width,hints.height,0,Pdepth,
		     InputOutput,Pvisual,CWBackPixel|CWBorderPixel|CWColormap,
		     &attr);

   wm_del_win=XInternAtom(dpy,"WM_DELETE_WINDOW",False);
   XSetWMProtocols(dpy,win,&wm_del_win,1);

   XSetWMNormalHints(dpy,win,&hints);

   XGrabButton(dpy,1,AnyModifier,win,True,GRAB_EVENTS,GrabModeAsync,
	       GrabModeAsync,None,None);
   XGrabButton(dpy,2,AnyModifier,win,True,GRAB_EVENTS,GrabModeAsync,
	       GrabModeAsync,None,None);
   XGrabButton(dpy,3,AnyModifier,win,True,GRAB_EVENTS,GrabModeAsync,
	       GrabModeAsync,None,None);

   /*   SetMwmHints(MWM_DECOR_ALL|MWM_DECOR_MAXIMIZE|MWM_DECOR_MINIMIZE,
	       MWM_FUNC_ALL|MWM_FUNC_MAXIMIZE|MWM_FUNC_MINIMIZE,
	       MWM_INPUT_MODELESS);
	       */

   CreateOrUpdateGCs();
   if (colorset >= 0)
   {
     SetWindowBackground(
       dpy, win, win_width, win_height, &Colorset[colorset],
       Pdepth, graph, False);
   }

   XSelectInput(dpy,win,(ExposureMask | KeyPressMask | PointerMotionMask |
                         EnterWindowMask | LeaveWindowMask |
                         StructureNotifyMask));
   /* ResizeRedirectMask |   */
   ChangeWindowName(&Module[1]);

   InitGoodies();
   atexit(ShutMeDown);
}

/******************************************************************************
  ShutMeDown - Do X client cleanup
******************************************************************************/
static void
ShutMeDown(void)
{
  FreeList(&windows);
  FreeAllButtons(&buttons);
  XFreeGC(dpy,graph);
  XFreeGC(dpy,icongraph);
  XDestroyWindow(dpy, win);
  XCloseDisplay(dpy);
}


/******************************************************************************
  ChangeWindowName - Self explanitory
    Original work from FvwmIdent:
      Copyright 1994, Robert Nation and Nobutaka Suzuki.
******************************************************************************/
void ChangeWindowName(char *str)
{
  XTextProperty name;
  if (XStringListToTextProperty(&str,1,&name) == 0) {
    fprintf(stderr,"%s: cannot allocate window name.\n",Module);
    return;
  }
  XSetWMName(dpy,win,&name);
  XSetWMIconName(dpy,win,&name);
  XFree(name.value);
}

/**************************************************************************
 *
 * Sets mwm hints
 *
 *************************************************************************/
/*
 *  Now, if we (hopefully) have MWW - compatible window manager ,
 *  say, mwm, ncdwm, or else, we will set useful decoration style.
 *  Never check for MWM_RUNNING property.May be considered bad.
 */

void SetMwmHints(unsigned int value, unsigned int funcs, unsigned int input)
{
PropMwmHints prop;

  if (MwmAtom==None)
    {
      MwmAtom=XInternAtom(dpy,"_MOTIF_WM_HINTS",False);
    }
  if (MwmAtom!=None)
    {
      /* sh->mwm.decorations contains OR of the MWM_DECOR_XXXXX */
      prop.decorations= value;
      prop.functions = funcs;
      prop.inputMode = input;
      prop.flags =
	MWM_HINTS_DECORATIONS | MWM_HINTS_FUNCTIONS | MWM_HINTS_INPUT_MODE;

      /* HOP - LA! */
      XChangeProperty (dpy,win,
		       MwmAtom, MwmAtom,
		       32, PropModeReplace,
		       (unsigned char *)&prop,
		       PROP_MWM_HINTS_ELEMENTS);
    }
}

/***********************************************************************
 *
 *  Procedure:
 *      ConstrainSize - adjust the given width and height to account for the
 *              constraints imposed by size hints
 *
 *      The general algorithm, especially the aspect ratio stuff, is
 *      borrowed from uwm's CheckConsistency routine.
 *
 ***********************************************************************/
void ConstrainSize (XSizeHints *hints, int *widthp, int *heightp)
{
#define makemult(a,b) ((b==1) ? (a) : (((int)((a)/(b))) * (b)) )
#define _min(a,b) (((a) < (b)) ? (a) : (b))


  int minWidth, minHeight, maxWidth, maxHeight, xinc, yinc, delta;
  int baseWidth, baseHeight;
  int dwidth = *widthp, dheight = *heightp;

  if(hints->flags & PMinSize)
    {
      minWidth = hints->min_width;
      minHeight = hints->min_height;
      if(hints->flags & PBaseSize)
	{
	  baseWidth = hints->base_width;
	  baseHeight = hints->base_height;
	}
      else
	{
	  baseWidth = hints->min_width;
	  baseHeight = hints->min_height;
	}
    }
  else if(hints->flags & PBaseSize)
    {
      minWidth = hints->base_width;
      minHeight = hints->base_height;
      baseWidth = hints->base_width;
      baseHeight = hints->base_height;
    }
  else
    {
      minWidth = 1;
      minHeight = 1;
      baseWidth = 1;
      baseHeight = 1;
    }

  if(hints->flags & PMaxSize)
    {
      maxWidth = hints->max_width;
      maxHeight = hints->max_height;
    }
  else
    {
      maxWidth = 10000;
      maxHeight = 10000;
    }
  if(hints->flags & PResizeInc)
    {
      xinc = hints->width_inc;
      yinc = hints->height_inc;
    }
  else
    {
      xinc = 1;
      yinc = 1;
    }

  /*
   * First, clamp to min and max values
   */
  if (dwidth < minWidth) dwidth = minWidth;
  if (dheight < minHeight) dheight = minHeight;

  if (dwidth > maxWidth) dwidth = maxWidth;
  if (dheight > maxHeight) dheight = maxHeight;


  /*
   * Second, fit to base + N * inc
   */
  dwidth = ((dwidth - baseWidth) / xinc * xinc) + baseWidth;
  dheight = ((dheight - baseHeight) / yinc * yinc) + baseHeight;


  /*
   * Third, adjust for aspect ratio
   */
#define maxAspectX hints->max_aspect.x
#define maxAspectY hints->max_aspect.y
#define minAspectX hints->min_aspect.x
#define minAspectY hints->min_aspect.y
  /*
   * The math looks like this:
   *
   * minAspectX    dwidth     maxAspectX
   * ---------- <= ------- <= ----------
   * minAspectY    dheight    maxAspectY
   *
   * If that is multiplied out, then the width and height are
   * invalid in the following situations:
   *
   * minAspectX * dheight > minAspectY * dwidth
   * maxAspectX * dheight < maxAspectY * dwidth
   *
   */

  if (hints->flags & PAspect)
    {
      if (minAspectX * dheight > minAspectY * dwidth)
	{
	  delta = makemult(minAspectX * dheight / minAspectY - dwidth,
			   xinc);
	  if (dwidth + delta <= maxWidth)
	    dwidth += delta;
	  else
	    {
	      delta = makemult(dheight - dwidth*minAspectY/minAspectX,
			       yinc);
	      if (dheight - delta >= minHeight) dheight -= delta;
	    }
	}

      if (maxAspectX * dheight < maxAspectY * dwidth)
	{
	  delta = makemult(dwidth * maxAspectY / maxAspectX - dheight,
			   yinc);
	  if (dheight + delta <= maxHeight)
	    dheight += delta;
	  else
	    {
	      delta = makemult(dwidth - maxAspectX*dheight/maxAspectY,
			       xinc);
	      if (dwidth - delta >= minWidth) dwidth -= delta;
	    }
	}
    }

  *widthp = dwidth;
  *heightp = dheight;
  return;
}

/***********************************************************************
 WarpTaskBar -- Enforce AutoStick feature
 ***********************************************************************/
void WarpTaskBar(int y)
{
  win_x = win_border;

  if (!AutoHide) {
     if (y < Midline)
       win_y = win_border;
     else
       win_y = (int)ScreenHeight - win_height - win_border;

     XSync(dpy, 0);
     XMoveWindow(dpy, win, win_x, win_y);
     XSync(dpy, 0);
  }

  if (AutoHide)
    SetAlarm(HIDE_TASK_BAR);

  /* Prevent oscillations caused by race with
     time delayed TaskBarHide().  Is there any way
     to prevent these Xevents from being sent
     to the server in the first place? */
  PurgeConfigEvents();
}

/***********************************************************************
 RevealTaskBar -- Make taskbar fully visible
 ***********************************************************************/
void RevealTaskBar()
{
  int new_win_y;

  ClearAlarm();

  if (win_y < Midline) {
    new_win_y = win_border;
    for (; win_y<=new_win_y; win_y+=2)
      XMoveWindow(dpy, win, win_x, win_y);
  } else {
    new_win_y = (int)ScreenHeight - win_height - win_border;
    for (; win_y>=new_win_y; win_y-=2)
      XMoveWindow(dpy, win, win_x, win_y);
  }

  win_y = new_win_y;
  XMoveWindow(dpy, win, win_x, win_y);
  BelayHide = False;
}

/***********************************************************************
 HideTaskbar -- Make taskbar partially visible
 ***********************************************************************/
void HideTaskBar()
{
  int new_win_y;

  ClearAlarm();

  if (win_y < Midline) {
    new_win_y = 1 - win_height;
    for (; win_y>=new_win_y; --win_y)
      XMoveWindow(dpy, win, win_x, win_y);
  } else {
    new_win_y = (int)ScreenHeight - 1;
    for (; win_y<=new_win_y; ++win_y)
      XMoveWindow(dpy, win, win_x, win_y);
  }

  win_y = new_win_y;

/*    XMoveWindow(dpy, win, win_x, win_y); */
}

/***********************************************************************
 SetAlarm -- Schedule a timeout event
 ************************************************************************/
static void
SetAlarm(int event)
{
  alarm(0);  /* remove a race-condition */
  AlarmSet = event;
  alarm(1);
}

/***********************************************************************
 ClearAlarm -- Disable timeout events
 ************************************************************************/
static void
ClearAlarm(void)
{
  AlarmSet = NOT_SET;
  alarm(0);
}

/***********************************************************************
 PurgeConfigEvents -- Wait for and purge ConfigureNotify events.
 ************************************************************************/
void PurgeConfigEvents(void)
{
  XEvent Event;

  XPeekEvent(dpy, &Event);
  while (XCheckTypedWindowEvent(dpy, win, ConfigureNotify, &Event))
    ;
}

/************************************************************************
  X Error Handler
************************************************************************/
static int
ErrorHandler(Display *d, XErrorEvent *event)
{
  PrintXErrorAndCoredump(d, event, Module);
  return 0;
}
