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

#include "srsran/adt/variant.h"
#include "srsran/asn1/e2ap/e2ap.h"
#include "srsran/asn1/e2ap/e2sm_kpm.h"
#include "srsran/asn1/e2ap/e2sm_rc.h"

namespace srsran {

struct e2_sm_event_trigger_definition_s {
  enum e2sm_ric_service_type_t { REPORT, INSERT, POLICY, UNKNOWN };
  e2sm_ric_service_type_t ric_service_type;
  uint64_t                report_period;
};

struct e2_sm_action_definition_s {
  enum e2sm_service_model_t { KPM, RC, UNKNOWN };
  e2sm_service_model_t                                                                                service_model;
  variant<asn1::e2sm_kpm::e2_sm_kpm_action_definition_s, asn1::e2sm_rc::e2_sm_rc_action_definition_s> action_definition;
};

class e2sm_report_service
{
public:
  virtual ~e2sm_report_service() = default;
  /// \brief Trigger collection of metric measurements.
  /// \return Returns True if collection was successful.
  virtual bool collect_measurements() = 0;
  /// \brief check if a valid indication message was created (i.e. if it does not contain only no_values).
  /// \return Returns True if the indication message is ready to be sent.
  virtual bool is_ind_msg_ready() = 0;
  /// \brief get the indication message with data collected by the report service.
  /// \return Returns the packed resultant Indication Message.
  virtual srsran::byte_buffer get_indication_message() = 0;
  /// \brief get the indication header with data generated by the report service.
  /// \return Returns the indication header.
  virtual srsran::byte_buffer get_indication_header() = 0;
};

class e2sm_handler
{
public:
  virtual ~e2sm_handler() = default;
  /// \brief Handle the packed E2SM Action Definition.
  /// \param[in] buf
  /// \return Returns the unpacked E2SM Action Definition.
  virtual e2_sm_action_definition_s handle_packed_e2sm_action_definition(const srsran::byte_buffer& buf) = 0;
  /// \brief Handle the packed E2SM Event Trigger Definition.
  /// \param[in] buf
  /// \return Returns the E2SM Event Trigger Definition.
  virtual e2_sm_event_trigger_definition_s handle_packed_event_trigger_definition(const srsran::byte_buffer& buf) = 0;
  /// @brief Pack the RAN function description.
  virtual asn1::unbounded_octstring<true> pack_ran_function_description() = 0;
};

class e2sm_interface
{
public:
  virtual ~e2sm_interface() = default;
  /// \brief gets a reference to the packer for this service model.
  /// \return Returns a reference to the packer.
  virtual e2sm_handler& get_e2sm_packer() = 0;
  /// \brief Check if the requested RIC action is supported
  /// \param[in] ric_action
  /// \return Returns true if action suppored by E2SM
  virtual bool action_supported(const asn1::e2ap::ri_caction_to_be_setup_item_s& ric_action) = 0;
  /// \brief gets a unique_ptr to the e2sm report service for an action
  /// \param[in] action
  /// \return Returns a unique_ptr to the e2sm report service
  virtual std::unique_ptr<e2sm_report_service>
  get_e2sm_report_service(const srsran::byte_buffer& action_definition) = 0;
};

} // namespace srsran