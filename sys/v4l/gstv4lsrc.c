/* G-Streamer BT8x8/V4L frame grabber plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <sys/time.h>
#include "v4lsrc_calls.h"


static GstElementDetails gst_v4lsrc_details = {
  "Video (video4linux/raw) Source",
  "Source/Video",
  "Reads raw frames from a video4linux (BT8x8) device",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2001",
};

/* V4lSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

/* arguments */
enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_PALETTE,
  ARG_PALETTE_NAME,
  ARG_NUMBUFS,
  ARG_BUFSIZE
};


/* init functions */
static void                  gst_v4lsrc_class_init   (GstV4lSrcClass *klass);
static void                  gst_v4lsrc_init         (GstV4lSrc      *v4lsrc);

/* pad/buffer functions */
static GstPadConnectReturn   gst_v4lsrc_srcconnect   (GstPad         *pad,
                                                      GstCaps        *caps);
static GstBuffer*            gst_v4lsrc_get          (GstPad         *pad);

/* get/set params */
static void                  gst_v4lsrc_set_property (GObject        *object,
                                                      guint          prop_id,
                                                      const GValue   *value,
                                                      GParamSpec     *pspec);
static void                  gst_v4lsrc_get_property (GObject        *object,
                                                      guint          prop_id,
                                                      GValue         *value,
                                                      GParamSpec     *pspec);

/* state handling */
static GstElementStateReturn gst_v4lsrc_change_state (GstElement     *element);

/* bufferpool functions */
static GstBuffer*            gst_v4lsrc_buffer_new   (GstBufferPool  *pool,
                                                      gint64         location,
                                                      gint           size,
                                                      gpointer       user_data);
static GstBuffer*            gst_v4lsrc_buffer_copy  (GstBuffer      *srcbuf);
static void                  gst_v4lsrc_buffer_free  (GstBuffer      *buf);


static GstCaps *capslist = NULL;
static GstPadTemplate *src_template;

static GstElementClass *parent_class = NULL;\
/*static guint gst_v4lsrc_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_v4lsrc_get_type (void)
{
  static GType v4lsrc_type = 0;

  if (!v4lsrc_type) {
    static const GTypeInfo v4lsrc_info = {
      sizeof(GstV4lSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_v4lsrc_class_init,
      NULL,
      NULL,
      sizeof(GstV4lSrc),
      0,
      (GInstanceInitFunc)gst_v4lsrc_init,
      NULL
    };
    v4lsrc_type = g_type_register_static(GST_TYPE_V4LELEMENT, "GstV4lSrc", &v4lsrc_info, 0);
  }
  return v4lsrc_type;
}


static void
gst_v4lsrc_class_init (GstV4lSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_V4LELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PALETTE,
    g_param_spec_int("palette","palette","palette",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PALETTE_NAME,
    g_param_spec_string("palette_name","palette_name","palette_name",
    NULL, G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUMBUFS,
    g_param_spec_int("num_buffers","num_buffers","num_buffers",
    G_MININT,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_int("buffer_size","buffer_size","buffer_size",
    G_MININT,G_MAXINT,0,G_PARAM_READABLE));

  gobject_class->set_property = gst_v4lsrc_set_property;
  gobject_class->get_property = gst_v4lsrc_get_property;

  gstelement_class->change_state = gst_v4lsrc_change_state;
}


static void
gst_v4lsrc_init (GstV4lSrc *v4lsrc)
{
  v4lsrc->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_element_add_pad(GST_ELEMENT(v4lsrc), v4lsrc->srcpad);

  gst_pad_set_get_function (v4lsrc->srcpad, gst_v4lsrc_get);
  gst_pad_set_connect_function (v4lsrc->srcpad, gst_v4lsrc_srcconnect);

  v4lsrc->bufferpool = gst_buffer_pool_new();
  gst_buffer_pool_set_buffer_new_function(v4lsrc->bufferpool, gst_v4lsrc_buffer_new);
  gst_buffer_pool_set_buffer_copy_function(v4lsrc->bufferpool, gst_v4lsrc_buffer_copy);
  gst_buffer_pool_set_buffer_free_function(v4lsrc->bufferpool, gst_v4lsrc_buffer_free);
  gst_buffer_pool_set_user_data(v4lsrc->bufferpool, v4lsrc);

  v4lsrc->palette = 0; /* means 'any' - user can specify a specific palette */
  v4lsrc->width = 160;
  v4lsrc->height = 120;
  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 1.5;

  v4lsrc->capslist = capslist;
}


static GstPadConnectReturn
gst_v4lsrc_srcconnect (GstPad  *pad,
                       GstCaps *vscapslist)
{
  GstV4lSrc *v4lsrc;
  GstCaps *caps, *newcaps;
  gint palette;

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  palette = v4lsrc->palette;

  /* TODO: caps = gst_caps_normalize(capslist); */
  for (caps = vscapslist ; caps != NULL ; caps = vscapslist = vscapslist->next)
  {
    guint32 fourcc;
    gint depth;

    gst_caps_get_fourcc_int (caps, "format", &fourcc);

    if (v4lsrc->palette > 0)
    {
      switch (v4lsrc->palette)
      {
        case VIDEO_PALETTE_YUV420P:
          if (fourcc != GST_MAKE_FOURCC('I','4','2','0') &&
              fourcc != GST_MAKE_FOURCC('I','Y','U','V'))
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 1.5;
          goto try_caps;
        case VIDEO_PALETTE_YUV422:
        case VIDEO_PALETTE_YUYV:
          if (fourcc != GST_MAKE_FOURCC('Y','U','Y','2'))
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          goto try_caps;
        case VIDEO_PALETTE_UYVY:
          if (fourcc != GST_MAKE_FOURCC('U','Y','V','Y'))
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          goto try_caps;
        case VIDEO_PALETTE_YUV411:
          if (fourcc != GST_MAKE_FOURCC('Y','4','1','P'))
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 1.5;
          goto try_caps;
        case VIDEO_PALETTE_RGB555:
	  depth = gst_caps_get_int (caps, "depth", &depth);
          if (fourcc != GST_MAKE_FOURCC('R','G','B',' ') ||
              depth != 15)
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          goto try_caps;
        case VIDEO_PALETTE_RGB565:
	  depth = gst_caps_get_int (caps, "depth", &depth);
          if (fourcc != GST_MAKE_FOURCC('R','G','B',' ') ||
              depth != 16)
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          goto try_caps;
        case VIDEO_PALETTE_RGB24:
	  depth = gst_caps_get_int (caps, "depth", &depth);
          if (fourcc != GST_MAKE_FOURCC('R','G','B',' ') ||
              depth != 24)
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 3;
          goto try_caps;
        case VIDEO_PALETTE_RGB32:
	  depth = gst_caps_get_int (caps, "depth", &depth);
          if (fourcc != GST_MAKE_FOURCC('R','G','B',' ') ||
              depth != 32)
            goto try_next;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 4;
          goto try_caps;
        default:
          goto try_next;
      }
    }
    else
    {
      switch (fourcc)
      {
        case GST_MAKE_FOURCC('I','4','2','0'):
        case GST_MAKE_FOURCC('I','Y','U','V'):
          palette = VIDEO_PALETTE_YUV420P;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 1.5;
          goto try_caps;
        case GST_MAKE_FOURCC('Y','U','Y','2'):
          palette = VIDEO_PALETTE_YUV422;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          goto try_caps;
        case GST_MAKE_FOURCC('U','Y','V','Y'):
          palette = VIDEO_PALETTE_UYVY;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
          goto try_caps;
        case GST_MAKE_FOURCC('Y','4','1','P'):
          palette = VIDEO_PALETTE_YUV411;
	  v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 1.5;
          goto try_caps;
        case GST_MAKE_FOURCC('R','G','B',' '):
	  depth = gst_caps_get_int (caps, "depth", &depth);
          switch (depth)
          {
            case 15:
              palette = VIDEO_PALETTE_RGB555;
	      v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
              goto try_caps;
            case 16:
              palette = VIDEO_PALETTE_RGB565;
	      v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 2;
              goto try_caps;
            case 24:
              palette = VIDEO_PALETTE_RGB24;
	      v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 3;
              goto try_caps;
            case 32:
              palette = VIDEO_PALETTE_RGB32;
	      v4lsrc->buffer_size = v4lsrc->width * v4lsrc->height * 4;
              goto try_caps;
            default:
              goto try_next;
          }
        default:
          goto try_next;
      }
    }

  /* if this caps wasn't useful, try the next one */
  try_next:
    continue;

  /* if this caps was useful, try it out */
  try_caps:
    /* TODO: try the current 'palette' out on the video device */

    if (!gst_v4lsrc_set_capture(v4lsrc, v4lsrc->width, v4lsrc->height, palette))
      continue;

    /* try to connect the pad/caps with the actual width/height */
    if (palette >= VIDEO_PALETTE_RGB565 && palette <= VIDEO_PALETTE_RGB555) {
       gint depth;
       gint bpp;

       gst_caps_get_int(caps, "bpp", &bpp),
       gst_caps_get_int(caps, "depth", &depth),

       newcaps = gst_caps_new("v4lsrc_caps",
                              "video/raw",
                              gst_props_new(
                                 "format", GST_PROPS_FOURCC(fourcc),
                                 "width",  GST_PROPS_INT(v4lsrc->width),
                                 "height", GST_PROPS_INT(v4lsrc->height),
                                 "bpp",    GST_PROPS_INT(bpp),
                                 "depth",  GST_PROPS_INT(depth),
                                 NULL      ) );
    }
    else {
       newcaps = gst_caps_new("v4lsrc_caps",
                              "video/raw",
                              gst_props_new(
                                 "format", GST_PROPS_FOURCC(fourcc),
                                 "width",  GST_PROPS_INT(v4lsrc->width),
                                 "height", GST_PROPS_INT(v4lsrc->height),
                                 NULL      ) );
    }

    gst_caps_debug (newcaps, "new caps to set on v4lsrc's src pad");

    if (!gst_pad_try_set_caps(v4lsrc->srcpad, newcaps))
      continue;
    else
      return GST_PAD_CONNECT_OK;
  }

  /* still nothing - no good caps */
  return GST_PAD_CONNECT_REFUSED;
}


static GstBuffer*
gst_v4lsrc_get (GstPad *pad)
{
  GstV4lSrc *v4lsrc;
  GstBuffer *buf;
  gint num;

  g_return_val_if_fail (pad != NULL, NULL);

  v4lsrc = GST_V4LSRC (gst_pad_get_parent (pad));

  buf = gst_buffer_new_from_pool(v4lsrc->bufferpool, 0, 0);
  if (!buf)
  {
    gst_element_error(GST_ELEMENT(v4lsrc),
      "Failed to create a new GstBuffer");
    return NULL;
  }

  /* grab a frame from the device */
  if (!gst_v4lsrc_grab_frame(v4lsrc, &num))
    return NULL;
  GST_BUFFER_DATA(buf) = gst_v4lsrc_get_buffer(v4lsrc, num);
  GST_BUFFER_SIZE(buf) = v4lsrc->buffer_size;
  buf->timestamp = v4lsrc->timestamp_soft_sync[num].tv_sec * 1000000000 +
    v4lsrc->timestamp_soft_sync[num].tv_usec * 1000;

  return buf;
}


static void
gst_v4lsrc_set_property (GObject      *object,
                         guint        prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail(GST_IS_V4LSRC(object));
  v4lsrc = GST_V4LSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      v4lsrc->width = g_value_get_int(value);
      break;

    case ARG_HEIGHT:
      v4lsrc->height = g_value_get_int(value);
      break;

    case ARG_PALETTE:
      v4lsrc->palette = g_value_get_int(value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_v4lsrc_get_property (GObject    *object,
                         guint      prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GstV4lSrc *v4lsrc;

  g_return_if_fail(GST_IS_V4LSRC(object));
  v4lsrc = GST_V4LSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int(value, v4lsrc->mmap.width);
      break;

    case ARG_HEIGHT:
      g_value_set_int(value, v4lsrc->mmap.height);
      break;

    case ARG_PALETTE:
      g_value_set_int(value, v4lsrc->mmap.format);
      break;

    case ARG_PALETTE_NAME:
      g_value_set_string(value, g_strdup(palette_name[v4lsrc->mmap.format]));
      break;

    case ARG_NUMBUFS:
      g_value_set_int(value, v4lsrc->mbuf.frames);
      break;

    case ARG_BUFSIZE:
      if (v4lsrc->mbuf.frames == 0)
        g_value_set_int(value, 0);
      else
        g_value_set_int(value, v4lsrc->mbuf.size/(v4lsrc->mbuf.frames*1024));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_v4lsrc_change_state (GstElement *element)
{
  GstV4lSrc *v4lsrc;
  gint transition = GST_STATE_TRANSITION (element);
  guint32 fourcc;
  gint depth=0, bpp=0;
  GstCaps *caps;

  g_return_val_if_fail(GST_IS_V4LSRC(element), GST_STATE_FAILURE);
  
  v4lsrc = GST_V4LSRC(element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      /* extremely ugly hack for a weird behaviour in the capsnego system - try capsnego again */
      switch (v4lsrc->mmap.format)
      {
        case VIDEO_PALETTE_RGB555:
          fourcc = GST_MAKE_FOURCC('R','G','B',' ');
          bpp = 16;
          depth = 15;
          break;
        case VIDEO_PALETTE_RGB565:
          fourcc = GST_MAKE_FOURCC('R','G','B',' ');
          bpp = 16;
          depth = 16;
          break;
        case VIDEO_PALETTE_RGB24:
          fourcc = GST_MAKE_FOURCC('R','G','B',' ');
          bpp = 24;
          depth = 24;
          break;
        case VIDEO_PALETTE_RGB32:
          fourcc = GST_MAKE_FOURCC('R','G','B',' ');
          bpp = 32;
          depth = 32;
          break;
        case VIDEO_PALETTE_YUV411:
          fourcc = GST_MAKE_FOURCC('Y','4','1','P');
          break;
        case VIDEO_PALETTE_YUV422:
          fourcc = GST_MAKE_FOURCC('Y','U','Y','2');
          break;
        case VIDEO_PALETTE_YUV420P:
          fourcc = GST_MAKE_FOURCC('I','4','2','0');
          break;
        case VIDEO_PALETTE_UYVY:
          fourcc = GST_MAKE_FOURCC('U','Y','V','Y');
          break;
	default:
	  return GST_STATE_FAILURE;
      }
      if (bpp && depth)
        caps = gst_caps_new("v4lsrc_caps",
                            "video/raw",
                            gst_props_new(
                              "format", GST_PROPS_FOURCC(fourcc),
                              "width",  GST_PROPS_INT(v4lsrc->width),
                              "height", GST_PROPS_INT(v4lsrc->height),
                              "bpp",    GST_PROPS_INT(bpp),
                              "depth",  GST_PROPS_INT(depth),
                              NULL      ) );
      else
        caps = gst_caps_new("v4lsrc_caps",
                            "video/raw",
                            gst_props_new(
                              "format", GST_PROPS_FOURCC(fourcc),
                              "width",  GST_PROPS_INT(v4lsrc->width),
                              "height", GST_PROPS_INT(v4lsrc->height),
                              NULL      ) );
      if (!gst_pad_try_set_caps(v4lsrc->srcpad, caps))
        return GST_STATE_FAILURE;

      if (!gst_v4lsrc_capture_init(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* queue all buffer, start streaming capture */
      if (!gst_v4lsrc_capture_start(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* de-queue all queued buffers */
      if (!gst_v4lsrc_capture_stop(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* stop capturing, unmap all buffers */
      if (!gst_v4lsrc_capture_deinit(v4lsrc))
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    GST_ELEMENT_CLASS (parent_class)->change_state (element);

  /* FIXME, this gives a not supported error on my machine?
  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      if ((GST_V4LELEMENT(v4lsrc)->norm >= VIDEO_MODE_PAL ||
           GST_V4LELEMENT(v4lsrc)->norm < VIDEO_MODE_AUTO) ||
          GST_V4LELEMENT(v4lsrc)->channel < 0)
        if (!gst_v4l_set_chan_norm(GST_V4LELEMENT(v4lsrc),
             0, GST_V4LELEMENT(v4lsrc)->norm))
          return GST_STATE_FAILURE;
      break;
  }
  */


  return GST_STATE_SUCCESS;
}


static GstBuffer*
gst_v4lsrc_buffer_new (GstBufferPool *pool,
                       gint64        location,
                       gint          size,
                       gpointer      user_data)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new();
  if (!buffer) return NULL;
  buffer->pool_private = user_data;

  /* TODO: add interlacing info to buffer as metadata (height>288 or 240 = topfieldfirst, else noninterlaced) */

  return buffer;
}


static GstBuffer*
gst_v4lsrc_buffer_copy (GstBuffer *srcbuf)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new();
  if (!buffer) return NULL;
  GST_BUFFER_DATA(buffer) = g_malloc(GST_BUFFER_SIZE(srcbuf));
  if (!GST_BUFFER_DATA(buffer)) return NULL;
  GST_BUFFER_SIZE(buffer) = GST_BUFFER_SIZE(srcbuf);
  memcpy(GST_BUFFER_DATA(buffer), GST_BUFFER_DATA(srcbuf), GST_BUFFER_SIZE(srcbuf));
  GST_BUFFER_TIMESTAMP(buffer) = GST_BUFFER_TIMESTAMP(srcbuf);

  return buffer;
}


static void
gst_v4lsrc_buffer_free (GstBuffer *buf)
{
  GstV4lSrc *v4lsrc = buf->pool_private;
  int n;

  for (n=0;n<v4lsrc->mbuf.frames;n++)
    if (GST_BUFFER_DATA(buf) == gst_v4lsrc_get_buffer(v4lsrc, n))
    {
      gst_v4lsrc_requeue_frame(v4lsrc, n);
      return;
    }

  gst_element_error(GST_ELEMENT(v4lsrc),
    "Couldn\'t find the buffer");
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstCaps *caps;
  gint i;
  gulong format[5] = { GST_MAKE_FOURCC('Y','U','Y','2'), /* VIDEO_PALETTE_YUV422/_YUYV */
                       GST_MAKE_FOURCC('I','4','2','0'), /* VIDEO_PALETTE_YUV420P */
                       GST_MAKE_FOURCC('I','Y','U','V'), /* VIDEO_PALETTE_YUV420P */
                       GST_MAKE_FOURCC('U','Y','V','Y'), /* VIDEO_PALETTE_UYVY */
                       GST_MAKE_FOURCC('Y','4','1','P')  /* VIDEO_PALETTE_YUV411 */
                     };
  gint rgb_bpp[4] = { 16, 16, 24, 32 };
  gint rgb_depth[4] = { 15, 16, 24, 32 };

  /* create an elementfactory for the v4lsrc */
  factory = gst_element_factory_new("v4lsrc",GST_TYPE_V4LSRC,
                                   &gst_v4lsrc_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* make a list of all available caps - first the YUV formats */
  for (i=0;i<5;i++)
  {
    caps = gst_caps_new ("v4lsrc_caps",
                         "video/raw",
                         gst_props_new (
                            "format", GST_PROPS_FOURCC(format[i]),
                            "width",  GST_PROPS_INT_RANGE (0, G_MAXINT),
                            "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                            NULL       )
                        );
    capslist = gst_caps_append(capslist, caps);
  }

  /* now all the RGB formats */
  for (i=0;i<4;i++)
  {
    caps = gst_caps_new ("v4lsrc_caps",
                         "video/raw",
                         gst_props_new (
                            "format", GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
                            "width",  GST_PROPS_INT_RANGE (0, G_MAXINT),
                            "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
                            "bpp",    GST_PROPS_INT(rgb_bpp[i]),
                            "depth",  GST_PROPS_INT(rgb_depth[i]),
                            NULL       )
                        );
    capslist = gst_caps_append(capslist, caps);
  }

  src_template = gst_pad_template_new (
		  "src",
                  GST_PAD_SRC,
  		  GST_PAD_ALWAYS,
		  capslist, NULL);

  gst_element_factory_add_pad_template (factory, src_template);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "v4lsrc",
  plugin_init
};
