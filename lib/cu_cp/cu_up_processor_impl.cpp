/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "cu_up_processor_impl.h"
#include "adapters/e1_adapters.h"
#include "adapters/ngc_adapters.h"
#include "procedures/initial_cu_up_processor_setup_procedure.h"
#include "srsgnb/e1_interface/cu_cp/e1_cu_cp_factory.h"

using namespace srsgnb;
using namespace srs_cu_cp;

cu_up_processor_impl::cu_up_processor_impl(const cu_up_processor_config_t             cu_up_processor_config_,
                                           cu_up_processor_cu_up_management_notifier& cu_cp_notifier_,
                                           e1_message_notifier&                       e1_notifier_,
                                           cu_up_processor_task_scheduler&            task_sched_) :
  cfg(cu_up_processor_config_),
  cu_cp_notifier(cu_cp_notifier_),
  e1_notifier(e1_notifier_),
  task_sched(task_sched_),
  main_ctrl_loop(128)
{
}

void cu_up_processor_impl::start()
{
  // create e1
  e1 = create_e1(task_sched.get_timer_manager(), e1_notifier, e1_ev_notifier);
  e1_ev_notifier.connect_cu_up_processor(*this);

  // Start E1 setup procedure
  cu_cp_e1_setup_request_message e1_setup_msg  = {};
  e1_setup_msg.request->gnb_cu_cp_name_present = true;
  e1_setup_msg.request->gnb_cu_cp_name.value.from_string(cfg.name);

  main_ctrl_loop.schedule<initial_cu_up_processor_setup_procedure>(context, *e1, cu_cp_notifier);
}

void cu_up_processor_impl::stop() {}

void cu_up_processor_impl::handle_cu_up_e1_setup_request(const srsgnb::cu_up_e1_setup_request_message& msg)
{
  // TODO: Handle setup request

  // send setup response
  send_cu_up_e1_setup_response();
}

/// Sender for F1C messages
void cu_up_processor_impl::send_cu_up_e1_setup_response()
{
  cu_up_e1_setup_response_message response;
  // TODO: fill message

  e1->handle_cu_up_e1_setup_response(response);
}

void cu_up_processor_impl::send_cu_up_e1_setup_failure(asn1::e1ap::cause_c::types::options cause)
{
  cu_up_e1_setup_response_message response;
  response.success = false;
  response.failure->cause->set(cause);
  e1->handle_cu_up_e1_setup_response(response);
}
