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

#include <stdio.h>
#include "libvmaf_wrapper.h"

GST_DEBUG_CATEGORY_STATIC (gst_vmaf_debug);
#define GST_CAT_DEFAULT gst_vmaf_debug
#define SINK_FORMATS " { I420 } "
#define SRC_FORMAT " { I420 } "
#define DEFAULT_MODEL_PATH       "/usr/local/share/model/vmaf_v0.6.1.pkl"
#define DEFAULT_LOG_PATH         NULL
#define DEFAULT_LOG_FMT          JSON_LOG_FMT
#define DEFAULT_DISABLE_CLIP     FALSE
//#define DEFAULT_DISABLE_AVX      FALSE
#define DEFAULT_ENABLE_TRANSFORM FALSE
#define DEFAULT_PHONE_MODEL      FALSE
#define DEFAULT_PSNR             FALSE
#define DEFAULT_SSIM             FALSE
#define DEFAULT_MS_SSIM          FALSE
#define DEFAULT_POOL_METHOD      MEAN_POOL_METHOD
#define DEFAULT_NUM_THREADS      0
#define DEFAULT_SUBSAMPLE        1
#define DEFAULT_CONF_INT         FALSE
#define GST_TYPE_VMAF_POOL_METHOD (gst_vmaf_pool_method_get_type ())
#define GST_TYPE_VMAF_LOG_FMT (gst_vmaf_log_fmt_get_type ())

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SRC_FORMAT))
    );

enum
{
  PROP_0,
  PROP_MODEL_PATH,
  PROP_LOG_PATH,
  PROP_LOG_FMT,
  PROP_DISABLE_CLIP,
  //PROP_DISABLE_AVX,
  PROP_ENABLE_TRANSFORM,
  PROP_PHONE_MODEL,
  PROP_PSNR,
  PROP_SSIM,
  PROP_MS_SSIM,
  PROP_POOL_METHOD,
  PROP_NUM_THREADS,
  PROP_SUBSAMPLE,
  PROP_CONF_INT,
  PROP_LAST,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SINK_FORMATS))
    );

static GType
gst_vmaf_pool_method_get_type (void)
{
  static const GEnumValue types[] = {
    {MIN_POOL_METHOD, "Minimum value", "min"},
    {MEAN_POOL_METHOD, "Arithmetic mean", "mean"},
    {HARMONIC_MEAN_POOL_METHOD, "Harmonic mean", "harmonic_mean"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType _id = g_enum_register_static ("GstVmafPoolMethod", types);
    g_once_init_leave (&id, _id);
  }

  return (GType) id;
}

static GType
gst_vmaf_log_fmt_get_type (void)
{
  static const GEnumValue types[] = {
    {JSON_LOG_FMT, "JSON format", "json"},
    //{XML_LOG_FMT, "XML format", "xml"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType _id = g_enum_register_static ("GstVmafLogFmt", types);
    g_once_init_leave (&id, _id);
  }

  return (GType) id;
}


/* GstVmaf */

static int
read_frame (float *ref_data, float *main_data, float *temp_data, int stride,
    void *h)
{
  GstVmafThreadHelper *helper = (GstVmafThreadHelper *) h;
  int ret;
  g_mutex_lock (&helper->wait_frame);
  if (!helper->no_frames) {
    int i, j;
    float *ref_ptr = ref_data;
    float *main_ptr = main_data;
    for (i = 0; i < helper->frame_height; i++) {
      for (j = 0; j < helper->frame_width; j++) {
        ref_ptr[j] = (float) helper->original_ptr[i * helper->frame_width + j];
        main_ptr[j] =
            (float) helper->distorted_ptr[i * helper->frame_width + j];
      }
      ref_ptr += stride / sizeof (*ref_data);
      main_ptr += stride / sizeof (*ref_data);
    }
    ret = 0;
    helper->reading_correct = TRUE;
    g_mutex_unlock (&helper->wait_reading_complete);
  } else {
    helper->reading_correct = FALSE;
    ret = 2;
  }
  return ret;
}

#define gst_vmaf_parent_class parent_class
G_DEFINE_TYPE (GstVmaf, gst_vmaf, GST_TYPE_VIDEO_AGGREGATOR);

static void
vmaf_thread_call (void *vs)
{
  GstVmafThreadHelper *helper;
  gboolean thread_is_stopped = FALSE;
  int error;
  const char *format;
  if (vs == NULL)
    return;
  helper = (GstVmafThreadHelper *) vs;
  g_mutex_lock (&helper->check_error);
  thread_is_stopped = helper->error != 0;
  g_mutex_unlock (&helper->check_error);
  if (thread_is_stopped)
    return;
  format = "yuv420p";           //  or "yuv420p10le"
  error = RunVMAF (format, read_frame, vs, helper);
  g_mutex_lock (&helper->check_error);
  helper->error = error;
  g_mutex_unlock (&helper->check_error);
  g_mutex_unlock (&helper->wait_reading_complete);
  if (helper->error)
    printf ("Error sink_%u: %d\n", helper->sink_index, helper->error);
  else
    printf ("VMAF sink_%u: %f\n", helper->sink_index, helper->score);
  return;
}

gboolean
try_thread_stop (GstTask * thread)
{
  GstTaskState task_state;
  gboolean result;
  task_state = gst_task_get_state (thread);
  if (task_state == GST_TASK_STARTED) {
    result = gst_task_stop (thread);
  } else {
    result = TRUE;
  }
  return result;
}

static gboolean
compare_frames (GstVmaf * self, GstVideoFrame * ref, GstVideoFrame * cmp,
    GstBuffer * outbuf, GstStructure * msg_structure, gchar * padname,
    guint stream_index)
{
  gboolean result;
  GstMapInfo ref_info;
  GstMapInfo cmp_info;
  GstMapInfo out_info;
  if (self->helper_struct_pointer[stream_index].frame_width == 0 ||
      self->helper_struct_pointer[stream_index].frame_height == 0) {
    self->helper_struct_pointer[stream_index].frame_width = ref->info.width;
    self->helper_struct_pointer[stream_index].frame_height = ref->info.height;
    gst_task_start (self->helper_struct_pointer[stream_index].vmaf_thread);
  }
  // Check that thread is waiting
  g_mutex_lock (&self->helper_struct_pointer[stream_index].check_error);
  if (self->helper_struct_pointer[stream_index].error) {
    try_thread_stop (self->helper_struct_pointer[stream_index].vmaf_thread);
    return FALSE;
  }
  g_mutex_unlock (&self->helper_struct_pointer[stream_index].check_error);
  self->helper_struct_pointer[stream_index].reading_correct = FALSE;
  // Run reading
  gst_buffer_map (ref->buffer, &ref_info, GST_MAP_READ);
  gst_buffer_map (cmp->buffer, &cmp_info, GST_MAP_READ);
  gst_buffer_map (outbuf, &out_info, GST_MAP_WRITE);
  self->helper_struct_pointer[stream_index].original_ptr = ref_info.data;
  self->helper_struct_pointer[stream_index].distorted_ptr = cmp_info.data;
  g_mutex_unlock (&self->helper_struct_pointer[stream_index].wait_frame);
  g_mutex_lock (&self->
      helper_struct_pointer[stream_index].wait_reading_complete);
  if (self->helper_struct_pointer[stream_index].reading_correct) {
    gint i;
    result = TRUE;
    for (i = 0; i < ref_info.size; i++) {
      out_info.data[i] = ref_info.data[i];
    }
  } else {
    g_mutex_lock (&self->helper_struct_pointer[stream_index].check_error);
    if (self->helper_struct_pointer[stream_index].error) {
      try_thread_stop (self->helper_struct_pointer[stream_index].vmaf_thread);
    }
    g_mutex_unlock (&self->helper_struct_pointer[stream_index].check_error);
    result = FALSE;
  }
  gst_buffer_unmap (ref->buffer, &ref_info);
  gst_buffer_unmap (cmp->buffer, &cmp_info);
  gst_buffer_unmap (outbuf, &out_info);
  return result;
}

static GstFlowReturn
gst_vmaf_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GList *l;
  GstVideoFrame *ref_frame = NULL;
  GstVmaf *self = GST_VMAF (vagg);
  GstStructure *msg_structure = gst_structure_new_empty ("VMAF");
  GstMessage *m = gst_message_new_element (GST_OBJECT (self), msg_structure);
  GstAggregator *agg = GST_AGGREGATOR (vagg);
  gboolean res = TRUE;
  guint stream_index = 0;

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstVideoFrame *prepared_frame =
        gst_video_aggregator_pad_get_prepared_frame (pad);

    if (prepared_frame != NULL) {
      if (!ref_frame) {
        ref_frame = prepared_frame;
      } else {
        gchar *padname = gst_pad_get_name (pad);
        GstVideoFrame *cmp_frame = prepared_frame;

        res &=
            compare_frames (self, ref_frame, cmp_frame, outbuf, msg_structure,
            padname, stream_index);
        g_free (padname);

        ++stream_index;
      }
    }
  }
  if (!res)
    goto failed;
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
gst_vmaf_set_log_fmt (GstVmaf * self, gint log_fmt)
{
  switch (log_fmt) {
    case JSON_LOG_FMT:
      self->log_fmt = JSON_LOG_FMT;
      break;
      //case XML_LOG_FMT:
      //  self->log_fmt = XML_LOG_FMT;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_vmaf_set_pool_method (GstVmaf * self, gint pool_method)
{
  switch (pool_method) {
    case MIN_POOL_METHOD:
      self->pool_method = MIN_POOL_METHOD;
      break;
    case MEAN_POOL_METHOD:
      self->pool_method = MEAN_POOL_METHOD;
      break;
    case HARMONIC_MEAN_POOL_METHOD:
      self->pool_method = HARMONIC_MEAN_POOL_METHOD;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_free (self->model_path);
      self->model_path = g_value_dup_string (value);
      break;
    case PROP_LOG_PATH:
      g_free (self->log_path);
      self->log_path = g_value_dup_string (value);
      break;
    case PROP_LOG_FMT:
      gst_vmaf_set_log_fmt (self, g_value_get_enum (value));
      break;
    case PROP_DISABLE_CLIP:
      self->vmaf_config_disable_clip = g_value_get_boolean (value);
      break;
      //case PROP_DISABLE_AVX:
      //  self->vmaf_config_disable_avx = g_value_get_boolean (value);
      //  break;
    case PROP_ENABLE_TRANSFORM:
      self->vmaf_config_enable_transform = g_value_get_boolean (value);
      break;
    case PROP_PHONE_MODEL:
      self->vmaf_config_phone_model = g_value_get_boolean (value);
      break;
    case PROP_PSNR:
      self->vmaf_config_psnr = g_value_get_boolean (value);
      break;
    case PROP_SSIM:
      self->vmaf_config_ssim = g_value_get_boolean (value);
      break;
    case PROP_MS_SSIM:
      self->vmaf_config_ms_ssim = g_value_get_boolean (value);
      break;
    case PROP_POOL_METHOD:
      gst_vmaf_set_pool_method (self, g_value_get_enum (value));
      break;
    case PROP_NUM_THREADS:
      self->num_threads = g_value_get_uint (value);
      break;
    case PROP_SUBSAMPLE:
      self->subsample = g_value_get_uint (value);
      break;
    case PROP_CONF_INT:
      self->vmaf_config_conf_int = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_MODEL_PATH:
      g_value_set_string (value, self->model_path);
      break;
    case PROP_LOG_PATH:
      g_value_set_string (value, self->log_path);
      break;
    case PROP_LOG_FMT:
      g_value_set_enum (value, self->log_fmt);
      break;
    case PROP_DISABLE_CLIP:
      g_value_set_boolean (value, self->vmaf_config_disable_clip);
      break;
      //case PROP_DISABLE_AVX:
      //  g_value_set_boolean (value, self->vmaf_config_disable_avx);
      //  break;
    case PROP_ENABLE_TRANSFORM:
      g_value_set_boolean (value, self->vmaf_config_enable_transform);
      break;
    case PROP_PHONE_MODEL:
      g_value_set_boolean (value, self->vmaf_config_phone_model);
      break;
    case PROP_PSNR:
      g_value_set_boolean (value, self->vmaf_config_psnr);
      break;
    case PROP_SSIM:
      g_value_set_boolean (value, self->vmaf_config_ssim);
      break;
    case PROP_MS_SSIM:
      g_value_set_boolean (value, self->vmaf_config_ms_ssim);
      break;
    case PROP_POOL_METHOD:
      g_value_set_enum (value, self->pool_method);
      break;
    case PROP_NUM_THREADS:
      g_value_set_uint (value, self->num_threads);
      break;
    case PROP_SUBSAMPLE:
      g_value_set_uint (value, self->subsample);
      break;
    case PROP_CONF_INT:
      g_value_set_boolean (value, self->vmaf_config_conf_int);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

/* GObject boilerplate */

static void
gst_vmaf_init (GstVmaf * self)
{
  GValue a = G_VALUE_INIT;
  g_value_init (&a, G_TYPE_STRING);
  g_value_set_static_string (&a, DEFAULT_MODEL_PATH);
  self->model_path = g_value_dup_string (&a);
  self->log_path = DEFAULT_LOG_PATH;
  gst_vmaf_set_log_fmt (self, DEFAULT_LOG_FMT);
  self->vmaf_config_disable_clip = DEFAULT_DISABLE_CLIP;
  //self->vmaf_config_disable_avx = DEFAULT_DISABLE_AVX;
  self->vmaf_config_enable_transform = DEFAULT_ENABLE_TRANSFORM;
  self->vmaf_config_phone_model = DEFAULT_PHONE_MODEL;
  self->vmaf_config_psnr = DEFAULT_PSNR;
  self->vmaf_config_ssim = DEFAULT_SSIM;
  self->vmaf_config_ms_ssim = DEFAULT_MS_SSIM;
  gst_vmaf_set_pool_method (self, DEFAULT_POOL_METHOD);
  self->num_threads = DEFAULT_NUM_THREADS;
  self->subsample = DEFAULT_SUBSAMPLE;
  self->vmaf_config_conf_int = DEFAULT_CONF_INT;
}

static gboolean
vmaf_threads_open (GstElement * element)
{
  GstVmaf *self = GST_VMAF (element);
  self->number_of_vmaf_threads = g_list_length (element->sinkpads);
  --self->number_of_vmaf_threads;       // Without reference
  self->helper_struct_pointer =
      g_malloc (sizeof (GstVmafThreadHelper) * self->number_of_vmaf_threads);
  for (guint i = 0; i < self->number_of_vmaf_threads; ++i) {
    self->helper_struct_pointer[i].gst_vmaf_p = self;
    self->helper_struct_pointer[i].no_frames = FALSE;
    self->helper_struct_pointer[i].reading_correct = FALSE;
    self->helper_struct_pointer[i].score = -1;
    self->helper_struct_pointer[i].error = 0;
    self->helper_struct_pointer[i].original_ptr = NULL;
    self->helper_struct_pointer[i].distorted_ptr = NULL;
    self->helper_struct_pointer[i].sink_index = i;
    self->helper_struct_pointer[i].frame_height = 0;
    self->helper_struct_pointer[i].frame_width = 0;
    g_mutex_init (&self->helper_struct_pointer[i].wait_frame);
    g_mutex_init (&self->helper_struct_pointer[i].wait_reading_complete);
    g_mutex_lock (&self->helper_struct_pointer[i].wait_frame);
    g_mutex_lock (&self->helper_struct_pointer[i].wait_reading_complete);
    g_mutex_init (&self->helper_struct_pointer[i].check_error);
    g_mutex_init (&self->helper_struct_pointer[i].check_error);
    g_rec_mutex_init (&self->helper_struct_pointer[i].vmaf_thread_mutex);
    self->helper_struct_pointer[i].vmaf_thread = gst_task_new (vmaf_thread_call,
        (void *) &self->helper_struct_pointer[i], NULL);
    gst_task_set_lock (self->helper_struct_pointer[i].vmaf_thread,
        &self->helper_struct_pointer[i].vmaf_thread_mutex);
  }
  return TRUE;
}

static gboolean
vmaf_threads_close (GstVmaf * self)
{
  for (int i = 0; i < self->number_of_vmaf_threads; ++i) {
    self->helper_struct_pointer[i].no_frames = TRUE;
    g_mutex_trylock (&self->helper_struct_pointer[i].wait_frame);
    g_mutex_unlock (&self->helper_struct_pointer[i].wait_frame);
  }
  for (int i = 0; i < self->number_of_vmaf_threads; ++i) {
    gst_task_join (self->helper_struct_pointer[i].vmaf_thread);
  }
  g_free (self->helper_struct_pointer);
  return TRUE;
}

static GstStateChangeReturn
gst_vmaf_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      vmaf_threads_open (element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      vmaf_threads_close (GST_VMAF (element));
      break;
    default:
      break;
  }
  return ret;
}

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

  g_object_class_install_property (gobject_class, PROP_MODEL_PATH,
      g_param_spec_string ("model-path", "model-path",
          "Model *.pkl filename", DEFAULT_MODEL_PATH, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_LOG_PATH,
      g_param_spec_string ("log-path", "log-path",
          "Results log filename", DEFAULT_LOG_PATH, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_LOG_FMT,
      g_param_spec_enum ("log-fmt", "log-fmt",
          "Set format for log", GST_TYPE_VMAF_LOG_FMT, DEFAULT_LOG_FMT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DISABLE_CLIP,
      g_param_spec_boolean ("disable-clip", "do-dssim",
          "Disable clipping VMAF values", DEFAULT_DISABLE_CLIP,
          G_PARAM_READWRITE));

  //g_object_class_install_property (gobject_class, PROP_DISABLE_AVX,
  //    g_param_spec_boolean ("disable-avx", "disable-avx",
  //        "Disable AVX intrinsics using", DEFAULT_DISABLE_AVX,
  //        G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENABLE_TRANSFORM,
      g_param_spec_boolean ("enable-transform", "enable-transform",
          "Enable transform VMAF scores", DEFAULT_ENABLE_TRANSFORM,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PHONE_MODEL,
      g_param_spec_boolean ("phone-model", "phone-model",
          "Use VMAF phone model", DEFAULT_PHONE_MODEL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PSNR,
      g_param_spec_boolean ("psnr", "psnr",
          "Estimate PSNR", DEFAULT_PSNR, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SSIM,
      g_param_spec_boolean ("ssim", "ssim",
          "Estimate SSIM", DEFAULT_SSIM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MS_SSIM,
      g_param_spec_boolean ("ms-ssim", "ms-ssim",
          "Estimate MS-SSIM", DEFAULT_MS_SSIM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_POOL_METHOD,
      g_param_spec_enum ("pool-method", "pool-method",
          "Pool method for mean", GST_TYPE_VMAF_POOL_METHOD,
          DEFAULT_POOL_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NUM_THREADS,
      g_param_spec_uint ("threads", "threads",
          "The number of threads",
          0, 32, DEFAULT_NUM_THREADS, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SUBSAMPLE,
      g_param_spec_uint ("subsample", "subsample",
          "Computing on one of every N frames",
          1, 128, DEFAULT_SUBSAMPLE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CONF_INT,
      g_param_spec_boolean ("conf-interval", "conf-interval",
          "Enable confidence intervals", DEFAULT_CONF_INT, G_PARAM_READWRITE));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_vmaf_change_state);

  gst_element_class_set_static_metadata (gstelement_class, "vmaf",
      "Filter/Analyzer/Video",
      "Provides Video Multi-Method Assessment Fusion metric",
      "Sergey Zvezdakov <szvezdakov@graphics.cs.msu.ru>");
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
