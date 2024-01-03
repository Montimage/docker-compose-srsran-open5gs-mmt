/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "srsran/rlc/rlc_tx_metrics.h"
#include "srsran/srslog/srslog.h"
#include <mutex>

namespace srsran {

class rlc_tx_metrics_container
{
  rlc_tx_metrics metrics = {};
  std::mutex     metrics_mutex;
  bool           enabled = false;

public:
  rlc_tx_metrics_container(bool enabled_) : enabled(enabled_) {}

  void metrics_set_mode(rlc_mode mode)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    metrics.mode = mode;
  }

  void metrics_add_sdus(uint32_t num_sdus_, size_t num_sdu_bytes_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    metrics.num_sdus += num_sdus_;
    metrics.num_sdu_bytes += num_sdu_bytes_;
  }

  void metrics_add_lost_sdus(uint32_t num_sdus_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    metrics.num_dropped_sdus += num_sdus_;
  }

  void metrics_add_pdus(uint32_t num_pdus_, size_t num_pdu_bytes_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    metrics.num_pdus += num_pdus_;
    metrics.num_pdu_bytes += num_pdu_bytes_;
  }

  void metrics_add_discard(uint32_t num_discarded_sdus_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    metrics.num_discarded_sdus += num_discarded_sdus_;
  }

  void metrics_add_discard_failure(uint32_t num_discard_failures_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    metrics.num_discard_failures += num_discard_failures_;
  }

  // TM specific metrics
  void metrics_add_small_alloc(uint32_t num_allocs_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    srsran_assert(metrics.mode == rlc_mode::tm, "Wrong mode for TM metrics.");
    metrics.mode_specific.tm.num_small_allocs += num_allocs_;
  }
  // UM specific metrics
  void metrics_add_segment(uint32_t num_segments_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    srsran_assert(metrics.mode == rlc_mode::um_bidir || metrics.mode == rlc_mode::um_unidir_dl,
                  "Wrong mode for UM metrics.");
    metrics.mode_specific.um.num_sdu_segments += num_segments_;
  }

  // AM specific metrics
  void metrics_add_retx_pdus(uint32_t num_retx_, uint32_t num_retx_pdu_bytes_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    srsran_assert(metrics.mode == rlc_mode::am, "Wrong mode for AM metrics.");
    metrics.mode_specific.am.num_retx_pdus += num_retx_;
    metrics.mode_specific.am.num_retx_pdu_bytes += num_retx_pdu_bytes_;
    metrics.num_pdus += num_retx_;
    metrics.num_pdu_bytes += num_retx_pdu_bytes_;
  }

  void metrics_add_ctrl_pdus(uint32_t num_ctrl_, uint32_t num_ctrl_pdu_bytes_)
  {
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    srsran_assert(metrics.mode == rlc_mode::am, "Wrong mode for AM metrics.");
    metrics.mode_specific.am.num_ctrl_pdus += num_ctrl_;
    metrics.mode_specific.am.num_ctrl_pdu_bytes += num_ctrl_pdu_bytes_;
    metrics.num_pdus += num_ctrl_;
    metrics.num_pdu_bytes += num_ctrl_pdu_bytes_;
  }

  // Metrics getters and setters
  rlc_tx_metrics get_metrics()
  {
    srsran_assert(enabled, "Trying to get metrics, but metrics are disabled.");
    if (not enabled) {
      return {};
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    return metrics;
  }

  rlc_tx_metrics get_and_reset_metrics()
  {
    srsran_assert(enabled, "Trying to get metrics, but metrics are disabled.");
    if (not enabled) {
      return {};
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    rlc_tx_metrics              ret = metrics;
    metrics                         = {};
    metrics.mode                    = ret.mode;
    return ret;
  }

  void reset_metrics()
  {
    srsran_assert(enabled, "Trying to reset metrics, but metrics are disabled.");
    if (not enabled) {
      return;
    }
    std::lock_guard<std::mutex> lock(metrics_mutex);
    rlc_mode                    tmp_mode = metrics.mode;
    metrics                              = {};
    metrics.mode                         = tmp_mode;
  }
};
} // namespace srsran
