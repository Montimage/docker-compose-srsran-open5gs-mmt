/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "rlc_tx_um_entity.h"
#include "rlc_um_pdu.h"

using namespace srsgnb;

rlc_tx_um_entity::rlc_tx_um_entity(du_ue_index_t                        du_index,
                                   rb_id_t                              rb_id,
                                   const rlc_tx_um_config&              config,
                                   rlc_tx_upper_layer_data_notifier&    upper_dn_,
                                   rlc_tx_upper_layer_control_notifier& upper_cn_,
                                   rlc_tx_lower_layer_notifier&         lower_dn_,
                                   task_executor&                       ue_executor_) :
  rlc_tx_entity(du_index, rb_id, upper_dn_, upper_cn_, lower_dn_),
  cfg(config),
  mod(cardinality(to_number(cfg.sn_field_length))),
  head_len_full(rlc_um_pdu_header_size_complete_sdu),
  head_len_first(rlc_um_pdu_header_size_no_so(cfg.sn_field_length)),
  head_len_not_first(rlc_um_pdu_header_size_with_so(cfg.sn_field_length)),
  ue_executor(ue_executor_)
{
}

// TS 38.322 v16.2.0 Sec. 5.2.2.1
void rlc_tx_um_entity::handle_sdu(rlc_sdu sdu_)
{
  size_t sdu_length = sdu_.buf.length();
  logger.log_info(sdu_.buf.begin(),
                  sdu_.buf.end(),
                  "TX SDU (length: {} B, PDCP Count: {}, enqueued SDUs: {})",
                  sdu_.buf.length(),
                  sdu_.pdcp_count,
                  sdu_queue.size_sdus());
  if (sdu_queue.write(sdu_)) {
    metrics.metrics_add_sdus(1, sdu_length);
    handle_buffer_state_update(); // take lock
  } else {
    logger.log_info("Dropped TX SDU (length: {} B, PDCP Count: {}, enqueued SDUs: {})",
                    sdu_length,
                    sdu_.pdcp_count,
                    sdu_queue.size_sdus());
    metrics.metrics_add_lost_sdus(1);
  }
}

// TS 38.322 v16.2.0 Sec. 5.4
void rlc_tx_um_entity::discard_sdu(uint32_t pdcp_count)
{
  logger.log_info("Discarding SDU with pdcp_count={}", pdcp_count);
  if (sdu_queue.discard(pdcp_count)) {
    metrics.metrics_add_discard(1);
    handle_buffer_state_update(); // take lock
  } else {
    logger.log_info("Could not discard SDU with pdcp_count={}", pdcp_count);
    metrics.metrics_add_discard_failure(1);
  }
}

// TS 38.322 v16.2.0 Sec. 5.2.2.1
byte_buffer_slice_chain rlc_tx_um_entity::pull_pdu(uint32_t grant_len)
{
  logger.log_debug("MAC opportunity: grant_len={}", grant_len);

  // Check available space -- we need at least the minimum header + 1 payload Byte
  if (grant_len <= head_len_full) {
    logger.log_debug("Cannot fit SDU into grant_len={}: head_len_full={}", grant_len, head_len_full);
    return {};
  }

  // Multiple threads can read from the SDU queue and change the
  // RLC UM TX state (current SDU, tx_next and next_so).
  // As such we need to lock to access these variables.
  std::lock_guard<std::mutex> lock(mutex);

  // Get a new SDU, if none is currently being transmitted
  if (sdu.buf.empty()) {
    srsgnb_sanity_check(next_so == 0, "New TX SDU, but next_so is not 0 (next_so={})", next_so);
    logger.log_debug(
        "Reading from SDU queue; status: {} SDUs, {} bytes", sdu_queue.size_sdus(), sdu_queue.size_bytes());
    if (not sdu_queue.read(sdu)) {
      logger.log_debug("No SDUs left in the SDU queue. grant_len={}", grant_len);
      return {};
    }
    logger.log_debug("Read SDU: SN={}, pdcp_count={}, sdu_len={}", st.tx_next, sdu.pdcp_count, sdu.buf.length());

    // Notify the upper layer about the beginning of the transfer of the current SDU
    if (sdu.pdcp_count.has_value()) {
      // Redirect upper layer notification to ue_executor
      auto handle_func = [this, pdcp_sn = std::move(sdu.pdcp_count.value())]() mutable {
        upper_dn.on_transmitted_sdu(pdcp_sn);
      };
      ue_executor.execute(std::move(handle_func));
    }
  }

  rlc_um_pdu_header header = {};
  header.sn                = st.tx_next;
  header.sn_size           = cfg.sn_field_length;
  header.so                = next_so;

  // Get SI and expected header size
  uint32_t head_len = 0;
  if (not get_si_and_expected_header_size(next_so, sdu.buf.length(), grant_len, header.si, head_len)) {
    logger.log_debug("Cannot fit {} into grant_len={}: head_len={}", header.si, grant_len, head_len);
    return {};
  }

  // Pack header
  byte_buffer header_buf = {};
  rlc_um_write_data_pdu_header(header, header_buf);
  srsgnb_sanity_check(head_len == header_buf.length(),
                      "Header length and expected header length do not match ({} != {})",
                      header_buf.length(),
                      head_len);

  // Sanity check: can this SDU be sent considering header overhead?
  // TODO: verify if this check is redundant; see get_si_and_expected_header_size() above
  if (grant_len <= head_len) {
    logger.log_debug("Cannot fit {} into grant_len={}: head_len={}", header.si, grant_len, head_len);
    return {};
  }

  // Calculate the amount of data to move
  uint32_t space       = grant_len - head_len;
  uint32_t payload_len = space >= sdu.buf.length() - next_so ? sdu.buf.length() - next_so : space;

  // Log PDU info
  logger.log_debug(
      "Creating PDU ({}) with {} B of {} B. grant_len={}", header.si, payload_len, sdu.buf.length(), grant_len);
  logger.log_debug("Creating PDU ({}): head_len={}, sdu_len={}, payload_len={}, grant_len={}",
                   header.si,
                   head_len,
                   sdu.buf.length(),
                   payload_len,
                   grant_len);

  // Assemble PDU
  byte_buffer_slice_chain pdu_buf = {};
  pdu_buf.push_front(std::move(header_buf));
  pdu_buf.push_back(byte_buffer_slice{sdu.buf, next_so, payload_len});

  // Release SDU if needed
  if (header.si == rlc_si_field::full_sdu || header.si == rlc_si_field::last_segment) {
    sdu.buf.clear();
    next_so = 0;
  } else {
    // advance SO offset
    next_so += payload_len;
  }

  // Update SN if needed
  if (header.si == rlc_si_field::last_segment) {
    st.tx_next = (st.tx_next + 1) % mod;
  }

  // Assert number of bytes
  srsgnb_assert(
      pdu_buf.length() <= grant_len, "Resulting pdu_len={} exceeds grant_len={}", pdu_buf.length(), grant_len);

  if (header.si == rlc_si_field::full_sdu) {
    // log without SN
    logger.log_info(pdu_buf.begin(),
                    pdu_buf.end(),
                    "TX PDU ({}): pdu_len={}, grant_len={}",
                    header.si,
                    pdu_buf.length(),
                    grant_len);
  } else {
    logger.log_info(pdu_buf.begin(),
                    pdu_buf.end(),
                    "TX PDU ({}): SN={}, SO={}, pdu_len={} grant_len={}",
                    header.si,
                    header.sn,
                    header.so,
                    pdu_buf.length(),
                    grant_len);
  }

  // Update metrics
  metrics.metrics_add_pdus(1, pdu_buf.length());
  handle_buffer_state_update_nolock(); // already locked

  // Log state
  log_state(srslog::basic_levels::debug);

  return pdu_buf;
}

/// Helper to get SI of an PDU
bool rlc_tx_um_entity::get_si_and_expected_header_size(uint32_t      so,
                                                       uint32_t      sdu_len,
                                                       uint32_t      grant_len,
                                                       rlc_si_field& si,
                                                       uint32_t&     head_len) const
{
  if (so == 0) {
    // Can we transmit the SDU fully?
    if (sdu_len <= grant_len - head_len_full) {
      // We can transmit the SDU fully
      si       = rlc_si_field::full_sdu;
      head_len = head_len_full;
    } else {
      // We can't transmit the SDU fully
      si       = rlc_si_field::first_segment;
      head_len = head_len_first;
      // Sanity check, do we have enough bytes for header?
      if (grant_len <= head_len_first) {
        return false;
      }
    }
  } else {
    // Sanity check, do we have enough bytes for header?
    if (grant_len <= head_len_not_first) {
      return false;
    }
    head_len = head_len_not_first;
    // Can we transmit the SDU segment fully?
    if (sdu_len - next_so <= grant_len - head_len_not_first) {
      // We can transmit the SDU fully
      si = rlc_si_field::last_segment;
    } else {
      // We can't transmit the SDU fully
      si = rlc_si_field::middle_segment;
    }
  }
  return true;
}

// TS 38.322 v16.2.0 Sec 5.5
uint32_t rlc_tx_um_entity::get_buffer_state()
{
  std::lock_guard<std::mutex> lock(mutex);
  return get_buffer_state_nolock();
}

void rlc_tx_um_entity::handle_buffer_state_update()
{
  std::lock_guard<std::mutex> lock(mutex);
  handle_buffer_state_update_nolock();
}

void rlc_tx_um_entity::handle_buffer_state_update_nolock()
{
  uint32_t bytes = get_buffer_state_nolock();
  logger.log_debug("Sending buffer state update to lower layer: {} B", bytes);
  lower_dn.on_buffer_state_update(bytes);
}

uint32_t rlc_tx_um_entity::get_buffer_state_nolock()
{
  // minimum bytes needed to tx all queued SDUs + each header
  uint32_t queue_bytes = sdu_queue.size_bytes() + sdu_queue.size_sdus() * head_len_full;

  // minimum bytes needed to tx SDU under segmentation + header (if applicable)
  uint32_t segment_bytes = 0;
  if (not sdu.buf.empty()) {
    segment_bytes = (sdu.buf.length() - next_so) + head_len_not_first;
  }

  return queue_bytes + segment_bytes;
}
