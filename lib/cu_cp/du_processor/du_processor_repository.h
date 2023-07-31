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

#include "../adapters/du_processor_adapters.h"
#include "../adapters/f1ap_adapters.h"
#include "../adapters/ngap_adapters.h"
#include "../cu_cp_impl_interface.h"
#include "../task_schedulers/du_task_scheduler.h"
#include "srsran/cu_cp/cell_meas_manager.h"
#include "srsran/cu_cp/cu_cp_types.h"
#include "srsran/cu_cp/du_processor.h"
#include "srsran/cu_cp/ue_manager.h"
#include <unordered_map>

namespace srsran {
namespace srs_cu_cp {

struct cu_cp_configuration;

struct du_repository_config {
  const cu_cp_configuration&          cu_cp;
  timer_manager&                      timers;
  cu_cp_du_event_handler&             cu_cp_du_handler;
  du_processor_e1ap_control_notifier& e1ap_ctrl_notifier;
  du_processor_ngap_control_notifier& ngap_ctrl_notifier;
  rrc_ue_nas_notifier&                ue_nas_pdu_notifier;
  rrc_ue_control_notifier&            ue_ngap_ctrl_notifier;
  rrc_ue_reestablishment_notifier&    rrc_ue_cu_cp_notifier;
  du_processor_ue_task_scheduler&     ue_task_sched;
  du_processor_ue_manager&            ue_manager;
  cell_meas_manager&                  cell_meas_mng;
  const std::atomic<bool>&            amf_connected;
  srslog::basic_logger&               logger;
};

class du_processor_repository : public du_repository, public cu_cp_du_repository_ngap_handler
{
public:
  explicit du_processor_repository(du_repository_config cfg_);

  // DU interface
  std::unique_ptr<f1ap_message_notifier>
         handle_new_du_connection(std::unique_ptr<f1ap_message_notifier> f1ap_tx_pdu_notifier) override;
  void   handle_du_remove_request(du_index_t du_index) override;
  size_t get_nof_dus() const override;
  size_t get_nof_ues() const override;

  du_handler& get_du(du_index_t du_index) override;
  du_index_t  find_du(pci_t pci) override;

  void handle_paging_message(cu_cp_paging_message& msg) override;

  ue_index_t handle_ue_index_allocation_request(const nr_cell_global_id_t& nci) override;
  void       handle_inter_ngran_node_n2_handover_request(const ngap_handover_request& request) override;

  void handle_amf_connection();
  void handle_amf_connection_drop();

  void request_ue_removal(du_index_t du_index, ue_index_t ue_index);

  void handle_inactivity_notification(du_index_t du_index, const cu_cp_inactivity_notification& msg);

private:
  struct du_context final : public du_handler {
    // CU-CP handler of DU processor events.
    du_processor_cu_cp_adapter du_to_cu_cp_notifier;

    // NGAP to DU processor notifier;
    ngap_du_processor_adapter ngap_du_processor_notifier;

    std::unique_ptr<du_processor_interface> du_processor;

    /// Notifier used by the CU-CP to push F1AP Tx messages to the respective DU.
    std::unique_ptr<f1ap_message_notifier> f1ap_tx_pdu_notifier;

    f1ap_statistics_handler&               get_f1ap_statistics_handler() override;
    f1ap_message_handler&                  get_f1ap_message_handler() override;
    du_processor_mobility_handler&         get_mobility_handler() override;
    du_processor_ue_task_handler&          get_du_processor_ue_task_handler() override;
    du_processor_f1ap_ue_context_notifier& get_f1ap_ue_context_notifier() override;
  };

  /// \brief Find a DU object.
  /// \param[in] du_index The index of the DU processor object.
  /// \return The DU processor object.
  du_processor_interface& find_du(du_index_t du_index);

  /// \brief Adds a DU processor object to the CU-CP.
  /// \return The DU index of the added DU processor object.
  du_index_t add_du(std::unique_ptr<f1ap_message_notifier> f1ap_tx_pdu_notifier);

  /// \brief Removes the specified DU processor object from the CU-CP.
  /// \param[in] du_index The index of the DU processor to delete.
  void remove_du(du_index_t du_index);

  /// \brief Get the next available index from the DU processor database.
  /// \return The DU index.
  du_index_t get_next_du_index();

  du_repository_config  cfg;
  srslog::basic_logger& logger;

  // F1AP to CU-CP adapter.
  f1ap_cu_cp_adapter f1ap_ev_notifier;

  du_task_scheduler du_task_sched;

  std::unordered_map<du_index_t, du_context> du_db;

  // TODO: DU removal not yet fully supported. Instead we just move the DU context to a separate map.
  std::unordered_map<du_index_t, du_context> removed_du_db;
};

} // namespace srs_cu_cp
} // namespace srsran
