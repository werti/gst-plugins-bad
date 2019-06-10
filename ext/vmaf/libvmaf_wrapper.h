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

#include "vmaf.h"

#ifdef __cplusplus
extern "C" {
#endif
int RunVMAF(
  const char* fmt,
  gint width,
  gint height,
  int (*read_frame)(float *ref_data, float *main_data, float *temp_data, int stride, void *user_data),
  void *user_data,
  const char *model_path,
  const char * log_path,
  GstVmafLogFmtEnum log_fmt,
  gboolean disable_clip,
  gboolean enable_transform,
  gboolean do_psnr,
  gboolean do_ssim,
  gboolean do_ms_ssim,
  GstVmafPoolMethodEnum pool_method,
  guint n_thread,
  guint n_subsample,
  gboolean enable_conf_interval,
  GstVmafPthreadHelper * pthread_helper);
#ifdef __cplusplus
}
#endif