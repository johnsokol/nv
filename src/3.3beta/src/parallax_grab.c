/*
	Netvideo version 3.3
	Written by Ron Frederick <frederick@parc.xerox.com>

	Parallax frame grab routines

	Portions of this software were taken from code written and
	copyrighted 1993 by Peter Schnorf <schnorf@canon.com>, and from
	an earlier version written and copyrighted 1993 by Henning
	Schulzrinne, AT&T Bell Laboratories <hgs@research.att.com>. The
	video grabber control panel code was based on code written and
	copyrighted 1993 by William C. Fenner <fenner@cmf.nrl.navy.mil>.
*/

/*
 * Copyright (c) Xerox Corporation 1993. All rights reserved.
 *
 * License is granted to copy, to use, and to make and to use derivative
 * works for research and evaluation purposes, provided that Xerox is
 * acknowledged in all documentation pertaining to any such copy or derivative
 * work. Xerox grants no other licenses expressed or implied. The Xerox trade
 * name should not be used in any advertising without its written permission.
 *
 * XEROX CORPORATION MAKES NO REPRESENTATIONS CONCERNING EITHER THE
 * MERCHANTABILITY OF THIS SOFTWARE OR THE SUITABILITY OF THIS SOFTWARE
 * FOR ANY PARTICULAR PURPOSE.  The software is provided "as is" without
 * express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this software.
 */

/*
 * Portions copyright (c) Canon Information Systems, Inc. 1993.
 * All rights reserved.
 *
 * License is granted to copy, to use, and to make and to use derivative
 * works for research and evaluation purposes, provided that Canon is
 * acknowledged in all documentation pertaining to any such copy or derivative
 * work. Canon grants no other licenses expressed or implied. The Canon trade
 * name should not be used in any advertising without its written permission.
 *
 * CANON INFORMATION SYSTEMS, INC. MAKES NO REPRESENTATIONS CONCERNING EITHER
 * THE MERCHANTABILITY OF THIS SOFTWARE OR THE SUITABILITY OF THIS SOFTWARE
 * FOR ANY PARTICULAR PURPOSE.  The software is provided "as is" without
 * express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this software.
 */

/*
 * Portions copyright (c) 1993  Naval Research Laboratory (NRL/CCS)
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the software,
 * derivative works or modified versions, and any portions thereof, and
 * that both notices appear in supporting documentation.
 *
 * NRL ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIM ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 */

#ifdef PARALLAX
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef __hpux
#include <Xvlib.h>
#endif
#include <XPlxExt.h>
#include <tcl.h>
#include <tk.h>
#include "sized_types.h"
#include "vid_image.h"
#include "vid_util.h"
#include "vid_code.h"
#include "parallax_grab.h"

extern Tk_Window tkMainWin;
extern Tcl_Interp *interp;

extern plx_IO *XPlxQueryConfig(Display *, Window, GC);
extern plx_signal *XPlxQueryVideo(Display *, Window, GC);

static Display *dpy;
static XVisualInfo vinfo;
static Window w;
static GC gc;
static int depth, top_skip, full_width, full_height;
static int xmit_size, xmit_color, width, height;
static reconfigproc_t *reconfig;
static ximage_t *ximage;

static int channels[2] = { PLX_INPUT_0, PLX_INPUT_1 };
static int formats[4] = { PLX_COMP, PLX_YC, PLX_RGB, PLX_YUV };
static int standards[3] = { PLX_NTSC, PLX_PAL, PLX_SECAM };

/*ARGSUSED*/
static int Parallax_ErrHandler(ClientData clientData, XErrorEvent *errevp)
{
    int *errp=(int *)clientData;

    *errp = 1;
    return 0;
}

/*ARGSUSED*/
static int PopCamera(ClientData clientData, XEvent *eventPtr)
{
    XVisibilityEvent *event = &eventPtr->xvisibility;
	
    if (eventPtr->type == VisibilityNotify && event->window == w) {
	    if (event->state != VisibilityUnobscured) {
		    XRaiseWindow(event->display, event->window);
		    XFlush(event->display);
		    return True;
	    }
    }
    return False;
}

static void Parallax_FindSignal(int *channelp, int *standardp, int *formatp)
{
    int chan, std, fmt, channel, standard, format;
    plx_signal *sig;

    for (channel=0; channel<2; channel++) {
	chan = channels[channel];
	for (format=0; format<4; format++) {
	    fmt = formats[format];
	    for (standard=0; standard<3; standard++) {
		std = standards[standard];
		XPlxVideoInputSelect(dpy, w, gc, chan, std, fmt, PLX_RGB24);
		sig = XPlxQueryVideo(dpy, w, gc);
		if (sig->sync_ok) {
		    *channelp = channel;
		    *standardp = standard;
		    *formatp = format;
		    return;
		}
	    }
	}
    }

    /* Default to input 0, composite, NTSC if no signal is found */
    *channelp = *standardp = *formatp = 0;
}

static void Parallax_InitSliders(void)
{
    int brightness, contrast, hue, saturation;
    char cmd[256];

    XPlxVideoValueQuery(dpy, w, gc, &saturation, &contrast, &hue, &brightness);
    sprintf(cmd, ".grabControls.parallax.row1.brightness set %d", brightness);
    Tcl_Eval(interp, cmd);
    sprintf(cmd, ".grabControls.parallax.row1.contrast set %d", contrast);
    Tcl_Eval(interp, cmd);
    sprintf(cmd, ".grabControls.parallax.row2.hue set %d", hue);
    Tcl_Eval(interp, cmd);
    sprintf(cmd, ".grabControls.parallax.row2.saturation set %d", saturation);
    Tcl_Eval(interp, cmd);
}

static int Parallax_Reconfig(void)
{
    plx_signal *sig;

    sig = XPlxQueryVideo(dpy, w, gc);
    top_skip = 0;

    switch (sig->standard) {
    default:
	printf("Unknown video standard %d (%dx%d)\n", sig->standard, sig->w, sig->h);
	break;
	
    case PLX_NTSC:
	full_width = 640;
	full_height = 480;
#ifdef __hpux
	top_skip = sig->h - full_height;
#else
	top_skip = 46;
#endif
	break;
    case PLX_PAL:
    case PLX_SECAM:
	full_width = 768;
	full_height = 576;
#ifdef __hpux
	top_skip = sig->h - full_height;
#else
	top_skip = 52;
#endif
	break;
    }

    switch (xmit_size) {
    case VID_SMALL:
	width = full_width/4;
	height = full_height/4;
	break;
    case VID_MEDIUM:
    default:
	width = full_width/2;
	height = full_height/2;
	break;
    case VID_LARGE:
	width = full_width;
	height = full_height;
	break;
    }

    if (top_skip < 0)
	    top_skip = 0;
    
    if (w != None) XResizeWindow(dpy, w, width, height);

    if (ximage != NULL) VidUtil_DestroyXImage(dpy, ximage);
#ifdef XX__hpux
    ximage = VidUtil_AllocStdXImage(dpy, vinfo.visual, depth, width, height);
#else
    ximage = VidUtil_AllocXImage(dpy, vinfo.visual, depth, width, height,
				 False);
#endif

    if (reconfig) (*reconfig)(xmit_color, width, height);
}

static void Parallax_GrabColor(uint8 *y_data, int8 *uv_data)
{
    int x, y;
    uint8 *yp=y_data, *ry=rgb2y;
    uint16 *uvp=(uint16 *)uv_data, *ruv=rgb2uv;
    uint32 p0, p1, *data=(uint32 *)ximage->image->data;

    for (y=0; y<height; y++) {
        for (x=0; x<width; x += 2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;
 
	    yp[0] = ry[((p0<<7) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>19) & 0x1f)];
	    yp[1] = ry[((p1<<7) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>19) & 0x1f)];
	    yp += 2;

	    p0 &= 0xfefeff;
	    p1 &= 0xfefeff;
	    p0 += p1;
	    *uvp++ = ruv[((p0<<6) & 0x7c00) +
			 ((p0>>7) & 0x3e0) +
			 ((p0>>20) & 0x1f)];
        }
    }
}

static void Parallax_GrabGrey(uint8 *y_data)
{
    int x, y;
    uint8 *yp=y_data, *ry=rgb2y;
    uint32 p0, p1, *data=(uint32 *)ximage->image->data;

    for (y=0; y<height; y++) {
        for (x=0; x<width; x += 2) {
	    p0 = data[0];
	    p1 = data[1];
	    data += 2;
 
	    yp[0] = ry[((p0<<7) & 0x7c00)+((p0>>6) & 0x3e0)+((p0>>19) & 0x1f)];
	    yp[1] = ry[((p1<<7) & 0x7c00)+((p1>>6) & 0x3e0)+((p1>>19) & 0x1f)];
	    yp += 2;
        }
    }
}

static int Parallax_GrabFrame(uint8 *y_data, int8 *uv_data)
{
    int err=0;
    Tk_ErrorHandler handler;

    XRaiseWindow(dpy, w);
    XPlxVideoTag(dpy, w, gc, PLX_VIDEO);
    XPlxVideoSqueezeStill(dpy, w, gc, 0, top_skip, full_width, full_height,
			  0, 0, width, height);
    XPlxVideoTag(dpy, w, gc, PLX_VIDEO_OVR);

    handler = Tk_CreateErrorHandler(dpy, -1, -1, -1, Parallax_ErrHandler, &err);
    VidUtil_GetXImage(dpy, w, 0, 0, ximage);
    Tk_DeleteErrorHandler(handler);
    if (err) return 0;

    if (xmit_color)
	Parallax_GrabColor(y_data, uv_data);
    else
	Parallax_GrabGrey(y_data);
     
    return 1;
}

static char *Parallax_SetAttr(ClientData clientData, Tcl_Interp *interp,
			      char *name1, char *name2, int flags)
{
    int brightness, contrast, hue, saturation;

    if (w != None) {
	brightness = atoi(Tcl_GetVar(interp, "plxBrightness", TCL_GLOBAL_ONLY));
	contrast = atoi(Tcl_GetVar(interp, "plxContrast", TCL_GLOBAL_ONLY));
	hue = atoi(Tcl_GetVar(interp, "plxHue", TCL_GLOBAL_ONLY));
	saturation = atoi(Tcl_GetVar(interp, "plxSaturation", TCL_GLOBAL_ONLY));
	XPlxVideoValueLoad(dpy, w, gc, saturation, contrast, hue, brightness);
    }
}

static char *Parallax_SetInput(ClientData clientData, Tcl_Interp *interp,
			       char *name1, char *name2, int flags)
{
    int chan, std, fmt;

    if (w != None) {
	chan = channels[atoi(Tcl_GetVar(interp, "plxChan", TCL_GLOBAL_ONLY))];
	std = standards[atoi(Tcl_GetVar(interp, "plxStd", TCL_GLOBAL_ONLY))];
	fmt = formats[atoi(Tcl_GetVar(interp, "plxFmt", TCL_GLOBAL_ONLY))];
	XPlxVideoInputSelect(dpy, w, gc, chan, std, fmt, PLX_RGB24);
	Parallax_InitSliders();
	Parallax_Reconfig();
    }
}

int Parallax_Probe(void)
{
    int majop, eventbase, errbase;
    
    dpy = Tk_Display(tkMainWin);
    
    if (XQueryExtension(dpy, "ParallaxVideo", &majop, &eventbase,
			&errbase) == False) {
#ifdef __hpux
	/*
	 * On HP-UX Parallax doesn't use the ParallaxVideo extension name.
	 * But it supports the XVideo extension. Therefore you need to
	 * query the adaptors for the PGItv2 grabber to verify that the
	 * Parallax is there.
	 */
        XvAdaptorInfo *grabber, *g;
	unsigned int ngrabbers, i;
	    
	if (XQueryExtension(dpy, "XVideo", &majop, &eventbase,
		            &errbase) == False)
	    return 0;

	XvQueryAdaptors(dpy, DefaultRootWindow(dpy), &ngrabbers, &grabber);
	for (i=0; i<ngrabbers; i++)
	    if (strncmp(grabber[i].name, "PGItv2", 6) == 0) break;
	if (i == ngrabbers)
#endif
		return 0;
    }
	    

    Tcl_TraceVar(interp, "plxBrightness", TCL_TRACE_WRITES,
		 Parallax_SetAttr, 0);
    Tcl_TraceVar(interp, "plxContrast", TCL_TRACE_WRITES,
		 Parallax_SetAttr, 0);
    Tcl_TraceVar(interp, "plxHue", TCL_TRACE_WRITES,
		 Parallax_SetAttr, 0);
    Tcl_TraceVar(interp, "plxSaturation", TCL_TRACE_WRITES,
		 Parallax_SetAttr, 0);
    Tcl_TraceVar(interp, "plxChan", TCL_TRACE_WRITES,
		 Parallax_SetInput, 0);
    Tcl_TraceVar(interp, "plxStd", TCL_TRACE_WRITES,
		 Parallax_SetInput, 0);
    Tcl_TraceVar(interp, "plxFmt", TCL_TRACE_WRITES,
		 Parallax_SetInput, 0);

    return VID_SMALL|VID_MEDIUM|VID_LARGE|VID_GREYSCALE|VID_COLOR;
}

char *Parallax_Attach(void)
{
    int screen=Tk_ScreenNumber(tkMainWin), mask;
    int channel, standard, format, vdepth;
    Window root;
    XSetWindowAttributes attr;
    char cmd[256];

    root = DefaultRootWindow(dpy);
#ifdef __hpux
    vdepth = depth = 24;
    if (XMatchVisualInfo(dpy, screen, vdepth, TrueColor, &vinfo) == 0) {
	vdepth = 8;
	vinfo.visual = DefaultVisual(dpy, screen);
    }
#else
    depth = ((strncmp(XServerVendor(dpy), "X11/NeWS", 8) == 0) ||
             (strncmp(XServerVendor(dpy), "Sun Micro", 9) == 0)) ? 24 : 32;
    vdepth = depth;
    if (XMatchVisualInfo(dpy, screen, vdepth, TrueColor, &vinfo) == 0) {
        fprintf(stderr, "Can't find a TrueColor visual with depth %d\n",vdepth);
	w = None;
	gc = None;
    } else
#endif
    {
	mask = CWBackPixel | CWColormap | CWBorderPixel;
	attr.colormap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);
	attr.background_pixel = attr.border_pixel = BlackPixel(dpy, screen);
	w = XCreateWindow(dpy, root, 0, 0, NTSC_WIDTH, NTSC_HEIGHT, 1, vdepth,
			  InputOutput, vinfo.visual, mask, &attr);
	XSetStandardProperties(dpy, w, "Local camera", "Camera", 0, 0, 0, 0);
	XSelectInput(dpy, w, VisibilityChangeMask);
	Tk_CreateGenericHandler(PopCamera, NULL);
	gc = XCreateGC(dpy, w, 0, 0);
    }

    Parallax_FindSignal(&channel, &standard, &format);
    sprintf(cmd, "set plxChan %d", channel);
    Tcl_Eval(interp, cmd);
    sprintf(cmd, "set plxStd %d", standard);
    Tcl_Eval(interp, cmd);
    sprintf(cmd, "set plxFmt %d", format);
    Tcl_Eval(interp, cmd);

    Parallax_InitSliders();
    return ".grabControls.parallax";
}

void Parallax_Detach(void)
{
    if (w != None) {
	XDestroyWindow(dpy, w);
	XFreeGC(dpy, gc);
	w = None;
	gc = None;
    }
}

/*ARGSUSED*/
grabproc_t *Parallax_Start(int max_framerate, int config, reconfigproc_t *r)
{
    reconfig = r;
    xmit_size = (config & VID_SIZEMASK);
    xmit_color = (config & VID_COLOR);

    if (w != None) {
	Parallax_Reconfig();
	XMapWindow(dpy, w);
	return Parallax_GrabFrame;
    } else {
	return NULL;
    }
}


void Parallax_Stop(void)
{
    VidUtil_DestroyXImage(dpy, ximage);
    XUnmapWindow(dpy, w);
    ximage = NULL;
    reconfig = NULL;
}
#endif /* PARALLAX */
