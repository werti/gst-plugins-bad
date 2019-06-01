/* Video Multi-Method Assessment Fusion plugin
 * Copyright (C) 2019 Sergey Zvezdakov <szvezdakov@graphics.cs.msu.ru>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-iqa
 * @title: iqa
 * @short_description: Image Quality Assessment plugin.
 *
 * IQA will perform full reference image quality assessment, with the
 * first added pad being the reference.
 *
 * It will perform comparisons on video streams with the same geometry.
 *
 * The image output will be the heat map of differences, between
 * the two pads with the highest measured difference.
 *
 * For each reference frame, IQA will post a message containing
 * a structure named IQA.
 *
 * The only metric supported for now is "dssim", which will be available
 * if https://github.com/pornel/dssim was installed on the system
 * at the time that plugin was compiled.
 *
 * For each metric activated, this structure will contain another
 * structure, named after the metric.
 *
 * The message will also contain a "time" field.
 *
 * For example, if do-dssim is set to true, and there are
 * two compared streams, the emitted structure will look like this:
 *
 * IQA, dssim=(structure)"dssim\,\ sink_1\=\(double\)0.053621271267184856\,\
 * sink_2\=\(double\)0.0082939683976297474\;",
 * time=(guint64)0;
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -m uridecodebin uri=file:///test/file/1 ! iqa name=iqa do-dssim=true \
 * ! videoconvert ! autovideosink uridecodebin uri=file:///test/file/2 ! iqa.
 * ]| This pipeline will output messages to the console for each set of compared frames.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vmaf.h"

#ifdef HAVE_RVMAF
//#include "dssim.h"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_vmaf_debug);
#define GST_CAT_DEFAULT gst_vmaf_debug

#define SINK_FORMATS " { I420 } "

#define SRC_FORMAT " { I420 } "

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SRC_FORMAT))
    );

enum
{
  PROP_0,
  PROP_DO_VMAF,
  PROP_LAST,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SINK_FORMATS))
    );


/* GstVmaf */

#define gst_vmaf_parent_class parent_class
G_DEFINE_TYPE (GstVmaf, gst_vmaf, GST_TYPE_VIDEO_AGGREGATOR);


static gboolean
compare_frames (GstVmaf * self, GstVideoFrame * ref, GstVideoFrame * cmp,
    GstBuffer * outbuf, GstStructure * msg_structure, gchar * padname)
{

  return TRUE;
}

static GstFlowReturn
gst_iqa_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GList *l;
  GstVideoFrame *ref_frame = NULL;
  GstVmaf *self = GST_VMAF (vagg);
  GstStructure *msg_structure = gst_structure_new_empty ("VMAF");
  GstMessage *m = gst_message_new_element (GST_OBJECT (self), msg_structure);
  GstAggregator *agg = GST_AGGREGATOR (vagg);

  //if (self->do_dssim) {
  //  gst_structure_set (msg_structure, "dssim", GST_TYPE_STRUCTURE,
  //      gst_structure_new_empty ("dssim"), NULL);
  //  self->max_dssim = 0.0;
  //}

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstVideoFrame *prepared_frame =
        gst_video_aggregator_pad_get_prepared_frame (pad);

    if (prepared_frame != NULL) {
      if (!ref_frame) {
        ref_frame = prepared_frame;
      } else {
        gboolean res;
        gchar *padname = gst_pad_get_name (pad);
        GstVideoFrame *cmp_frame = prepared_frame;

        res = compare_frames (self, ref_frame, cmp_frame, outbuf, msg_structure,
            padname);
        g_free (padname);

        if (!res)
          goto failed;
      }
    }
  }

  GST_OBJECT_UNLOCK (vagg);

  /* We only post the message here, because we can't post it while the object
   * is locked.
   */
  gst_structure_set (msg_structure, "time", GST_TYPE_CLOCK_TIME,
      GST_AGGREGATOR_PAD (agg->srcpad)->segment.position, NULL);
  gst_element_post_message (GST_ELEMENT (self), m);
  return GST_FLOW_OK;

failed:
  GST_OBJECT_UNLOCK (vagg);

  return GST_FLOW_ERROR;
}

static void
_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  switch (prop_id) {
    case PROP_DO_VMAF:
      GST_OBJECT_LOCK (self);
      self->do_vmaf = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  switch (prop_id) {
    case PROP_DO_VMAF:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, self->do_vmaf);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GObject boilerplate */
static void
gst_vmaf_class_init (GstVmafClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;

  videoaggregator_class->aggregate_frames = gst_vmaf_aggregate_frames;

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &sink_factory, GST_TYPE_VIDEO_AGGREGATOR_CONVERT_PAD);

  gobject_class->set_property = _set_property;
  gobject_class->get_property = _get_property;

#ifdef HAVE_RVMAF
  g_object_class_install_property (gobject_class, PROP_DO_VMAF,
      g_param_spec_boolean ("do-vmaf", "do-vmaf",
          "Run VMAF metric", FALSE, G_PARAM_READWRITE));
#endif

  gst_element_class_set_static_metadata (gstelement_class, "vmaf",
      "Filter/Analyzer/Video",
      "Provides Video Multi-Method Assessment Fusion metric",
      "Sergey Zvezdakov <szvezdakov@graphics.cs.msu.ru>");
}

static void
gst_vmaf_init (GstVmaf * self)
{
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_vmaf_debug, "vmaf", 0, "vmaf");

  return gst_element_register (plugin, "vmaf", GST_RANK_PRIMARY, GST_TYPE_VMAF);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vmaf,
    "vmaf", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
