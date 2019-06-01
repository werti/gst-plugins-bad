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

/**
 * GstVmaf:
 *
 * The opaque #GstVmaf structure.
 */
struct _GstVmaf
{
  GstVideoAggregator videoaggregator;

  gboolean do_vmaf;
};

struct _GstVmafClass
{
  GstVideoAggregatorClass parent_class;
};

GType gst_vmaf_get_type (void);

G_END_DECLS
#endif /* __GST_VMAF_H__ */

