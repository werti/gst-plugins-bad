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

#include <cmath>
#include <memory>
#include <string>
#include <iostream>
#include <fstream>
#include <limits>
#include <libvmaf.h>
#include "libvmaf_wrapper.h"

typedef std::numeric_limits< double > dbl;
static const char *VmafPoolMethodNames[] = {
    "min",
    "mean",
    "harmonic_mean"
};

static const std::string BOOSTRAP_VMAF_MODEL_PREFIX = "vmaf_";

std::string _get_file_name(const std::string& s);

int RunVMAF(
  int (*read_frame)(float *ref_data, float *main_data, float *temp_data, int stride, void *user_data),
  void *user_data,
  GstVmafThreadHelper * thread_helper)
{
  int width = thread_helper->frame_width;
  int height = thread_helper->frame_height;
  const char * model_path = thread_helper->gst_vmaf_p->model_path;
  const char * log_path = thread_helper->gst_vmaf_p->log_path;
  const char * fmt;
  GstVmafPoolMethodEnum pool_method = thread_helper->gst_vmaf_p->pool_method;
  int n_subsample = thread_helper->gst_vmaf_p->subsample;

  if (thread_helper->y10bit)
    fmt = "yuv420p10le";
  else
    fmt = "yuv420p";

  Result result;
  try {
    Asset asset(width, height, fmt);
    std::unique_ptr<IVmafQualityRunner> runner_ptr =
        VmafQualityRunnerFactory::createVmafQualityRunner(
        model_path, thread_helper->gst_vmaf_p->vmaf_config_conf_int);
    result = runner_ptr->run(asset, read_frame, user_data,
        thread_helper->gst_vmaf_p->vmaf_config_disable_clip,
        (thread_helper->gst_vmaf_p->vmaf_config_enable_transform
          || thread_helper->gst_vmaf_p->vmaf_config_phone_model),
        thread_helper->gst_vmaf_p->vmaf_config_psnr,
        thread_helper->gst_vmaf_p->vmaf_config_ssim,
        thread_helper->gst_vmaf_p->vmaf_config_ms_ssim,
        thread_helper->gst_vmaf_p->num_threads,
        n_subsample);
  }
  catch (std::runtime_error& e)
  {
    printf("Caught runtime_error: %s\n", e.what());
    return -3;
  }
  catch (std::logic_error& e)
  {
    printf("Caught logic_error: %s\n", e.what());
    return -4;
  }
  catch (std::exception& e)
  {
    printf("Caught exception: %s\n", e.what());
    return -5;
  }
  catch (...)
  {
    printf("Unknown exception!\n");
    return -6;
  }
  switch (pool_method) {
    case MIN_POOL_METHOD:
      result.setScoreAggregateMethod(ScoreAggregateMethod::MINIMUM);;
      break;
    case MEAN_POOL_METHOD:
      result.setScoreAggregateMethod(ScoreAggregateMethod::MEAN);
      break;
    case HARMONIC_MEAN_POOL_METHOD:
      result.setScoreAggregateMethod(ScoreAggregateMethod::HARMONIC_MEAN);
      break;
  }
  double aggregate_vmaf = result.get_score("vmaf");
  thread_helper->score = aggregate_vmaf;
  thread_helper->error = 0;
  std::vector<std::string> result_keys = result.get_keys();
  double aggregate_bagging = 0.0, aggregate_stddev = 0.0;
  double aggregate_ci95_low = 0.0, aggregate_ci95_high = 0.0;
  if (result.has_scores("bagging"))
    aggregate_bagging = result.get_score("bagging");
  if (result.has_scores("stddev"))
    aggregate_stddev = result.get_score("stddev");
  if (result.has_scores("ci95_low"))
    aggregate_ci95_low = result.get_score("ci95_low");
  if (result.has_scores("ci95_high"))
    aggregate_ci95_high = result.get_score("ci95_high");

  double aggregate_psnr = 0.0, aggregate_ssim = 0.0, aggregate_ms_ssim = 0.0;
  if (result.has_scores("psnr"))
    aggregate_psnr = result.get_score("psnr");
  if (result.has_scores("ssim"))
    aggregate_ssim = result.get_score("ssim");
  if (result.has_scores("ms_ssim"))
    aggregate_ms_ssim = result.get_score("ms_ssim");

  printf("VMAF score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_vmaf);
  if (aggregate_bagging)
    printf("Bagging score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_bagging);
  if (aggregate_stddev)
    printf("StdDev score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_stddev);
  if (aggregate_ci95_low)
    printf("CI95_low score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_ci95_low);
  if (aggregate_ci95_high)
    printf("CI95_high score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_ci95_high);
  if (aggregate_psnr)
    printf("PSNR score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_psnr);
  if (aggregate_ssim)
    printf("SSIM score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_ssim);
  if (aggregate_ms_ssim)
    printf("MS-SSIM score (%s) = %f\n", VmafPoolMethodNames[pool_method], aggregate_ms_ssim);

  int num_bootstrap_models = 0;
  std::string bootstrap_model_list_str = "";

  // determine number of bootstrap models (if any) and construct a comma-separated string
  // of bootstrap vmaf model names
  for (size_t j = 0; j < result_keys.size(); j++)
  {
    if (result_keys[j].find(BOOSTRAP_VMAF_MODEL_PREFIX)!= std::string::npos)
    {
      if (num_bootstrap_models == 0)
      {
        bootstrap_model_list_str += result_keys[j] + ",";
      }
      else if (num_bootstrap_models == 1)
      {
        bootstrap_model_list_str += result_keys[j];
      }
      else
      {
        bootstrap_model_list_str += "," + result_keys[j];
      }
      printf("VMAF score (%s), model %d = %f\n", VmafPoolMethodNames[pool_method],
          num_bootstrap_models + 1,
          result.get_score(result_keys[j]));
      num_bootstrap_models += 1;
    }
  }
  if (log_path != NULL && thread_helper->gst_vmaf_p->log_fmt == JSON_LOG_FMT)
  {
    size_t num_frames_subsampled = result.get_scores("vmaf").size();
    std::ofstream log_file(log_path);
    log_file.precision(dbl::max_digits10);
    log_file << "{" << std::endl;
    log_file << "  \"params\":{" << std::endl;
    log_file << "    \"model\":\"" << _get_file_name(std::string(model_path)) << "\"," << std::endl;
    log_file << "    \"scaledWidth\":" << width << "," << std::endl;
    log_file << "    \"scaledHeight\":" << height << "," << std::endl;
    log_file << "    \"subsample\":" << n_subsample << "," << std::endl;
    log_file << "    \"num_bootstrap_models\":" << num_bootstrap_models << "," << std::endl;
    log_file << "    \"bootstrap_model_list_str\":\"" << bootstrap_model_list_str << "\"" << std::endl;
    log_file << "  }," << std::endl;
    log_file << "  \"metrics\":[" << std::endl;
    for (size_t j = 0; j < result_keys.size(); j++)
    {
      log_file << "    \"" << result_keys[j] << "\"";
      if (j < result_keys.size()-1)
        log_file << ",";
      log_file << std::endl;
    }
    log_file << "  ]," << std::endl;
    log_file << "  \"frames\":[" << std::endl;
    for (size_t i_subsampled=0; i_subsampled<num_frames_subsampled; i_subsampled++)
    {
      log_file << "    {" << std::endl;
      log_file << "      \"frameNum\":" << i_subsampled * n_subsample << "," << std::endl;
      log_file << "      \"metrics\":{" << std::endl;
      for (size_t j = 0; j < result_keys.size(); j++)
      {
        double value = result.get_scores(result_keys[j].c_str()).at(i_subsampled);
        log_file << "        \"" << result_keys[j] << "\":" << value;
        if (j == result_keys.size()-1) 
          log_file << std::endl;
        else
          log_file << "," << std::endl;
      }
      log_file << "      }" << std::endl;
      if (i_subsampled == num_frames_subsampled-1) 
        log_file << "    }" << std::endl;
      else
        log_file << "    }," << std::endl;
    }
    log_file << "  ]," << std::endl;
    log_file << "  \"VMAF score\":" << aggregate_vmaf;
    if (aggregate_psnr) {
      log_file << "," << std::endl;
      log_file << "  \"PSNR score\":" << aggregate_psnr;
    }
    if (aggregate_ssim) {
      log_file << "," << std::endl;
      log_file << "  \"SSIM score\":" << aggregate_ssim;
    }
    if (aggregate_ms_ssim) {
      log_file << "," << std::endl;
      log_file << "  \"MS-SSIM score\":" << aggregate_ms_ssim;
    }
    log_file << std::endl;
    log_file << "}" << std::endl;
    log_file.close();
  }
  return 0;
}
