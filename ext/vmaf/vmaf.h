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

#ifndef __GST_VMAF_H__
#define __GST_VMAF_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>

G_BEGIN_DECLS

#define GST_TYPE_VMAF (gst_vmaf_get_type())
#define GST_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VMAF, GstVmaf))
#define GST_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VMAF, GstVmafClass))
#define GST_IS_VMAF(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VMAF))
#define GST_IS_VMAF_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VMAF))

typedef struct _GstVmaf GstVmaf;
typedef struct _GstVmafClass GstVmafClass;

typedef enum _GstVmafLogFmtEnum
{
  JSON_LOG_FMT = 0,
  //XML_LOG_FMT = 1,
} GstVmafLogFmtEnum;

typedef enum _GstVmafPoolMethodEnum
{
  MIN_POOL_METHOD = 0,
  MEAN_POOL_METHOD = 1,
  HARMONIC_MEAN_POOL_METHOD = 2
} GstVmafPoolMethodEnum;

typedef struct {
  GstVmaf * gst_vmaf_p;
  GstTask * vmaf_thread;
  GRecMutex vmaf_thread_mutex;
  GMutex wait_frame;
  GMutex wait_reading_complete;
  GMutex check_error;
  gboolean no_frames;
  gboolean reading_correct;
  gdouble score;
  int error;
  guint sink_index;
  guint8 *original_ptr_8bit;
  guint8 *distorted_ptr_8bit;
  guint16 *original_ptr_10bit;
  guint16 *distorted_ptr_10bit;
  gint frame_height;
  gint frame_width;
  gboolean y10bit;
} GstVmafThreadHelper;

/**
 * GstVmaf:
 *
 * The opaque #GstVmaf structure.
 */
struct _GstVmaf
{
  GstVideoAggregator videoaggregator;
  // VMAF settings from cmd
  gchar * model_path;
  gchar * log_path;
  GstVmafLogFmtEnum log_fmt;
  gboolean vmaf_config_disable_clip;
  //gboolean vmaf_config_disable_avx;
  gboolean vmaf_config_enable_transform;
  gboolean vmaf_config_phone_model;
  gboolean vmaf_config_psnr;
  gboolean vmaf_config_ssim;
  gboolean vmaf_config_ms_ssim;
  GstVmafPoolMethodEnum pool_method;
  guint num_threads;
  guint subsample;
  gboolean vmaf_config_conf_int;
  // Thread helpers
  GstVmafThreadHelper * helper_struct_pointer;
  gint number_of_vmaf_threads;
  GMutex print_results;
};

struct _GstVmafClass
{
  GstVideoAggregatorClass parent_class;
};

GType gst_vmaf_get_type (void);

G_END_DECLS
#endif /* __GST_VMAF_H__ */
