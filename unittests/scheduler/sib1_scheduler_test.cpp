/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "lib/scheduler/common_scheduling/sib_scheduler.h"
#include "lib/scheduler/common_scheduling/ssb_scheduler.h"
#include "scheduler_test_suite.h"
#include "srsgnb/support/test_utils.h"
#include "unittests/scheduler/utils/config_generators.h"

using namespace srsgnb;

// Dummy PDCCH scheduler required to instantiate the SIB1 scheduler.
class dummy_pdcch_resource_allocator : public pdcch_resource_allocator
{
public:
  pdcch_dl_information* alloc_pdcch_common(cell_slot_resource_allocator& slot_alloc,
                                           rnti_t                        rnti,
                                           search_space_id               ss_id,
                                           aggregation_level             aggr_lvl) override
  {
    TESTASSERT_EQ(ss_id, slot_alloc.cfg.dl_cfg_common.init_dl_bwp.pdcch_common.sib1_search_space_id);
    slot_alloc.result.dl.dl_pdcchs.emplace_back();
    slot_alloc.result.dl.dl_pdcchs.back().ctx.rnti    = rnti;
    slot_alloc.result.dl.dl_pdcchs.back().ctx.bwp_cfg = &slot_alloc.cfg.dl_cfg_common.init_dl_bwp.generic_params;
    slot_alloc.result.dl.dl_pdcchs.back().ctx.coreset_cfg =
        &*slot_alloc.cfg.dl_cfg_common.init_dl_bwp.pdcch_common.coreset0;
    slot_alloc.result.dl.dl_pdcchs.back().ctx.cces = {0, srsgnb::aggregation_level::n4};
    return &slot_alloc.result.dl.dl_pdcchs[0];
  }

  pdcch_dl_information* alloc_dl_pdcch_ue(cell_slot_resource_allocator& slot_alloc,
                                          rnti_t                        rnti,
                                          const ue_cell_configuration&  user,
                                          bwp_id_t                      bwp_id,
                                          search_space_id               ss_id,
                                          aggregation_level             aggr_lvl) override
  {
    srsgnb_terminate("UE-dedicated PDCCHs should not be called while allocating RARs");
    return nullptr;
  }

  pdcch_ul_information* alloc_ul_pdcch_ue(cell_slot_resource_allocator& slot_alloc,
                                          rnti_t                        rnti,
                                          const ue_cell_configuration&  user,
                                          bwp_id_t                      bwp_id,
                                          search_space_id               ss_id,
                                          aggregation_level             aggr_lvl) override
  {
    srsgnb_terminate("UE-dedicated PDCCHs should not be called while allocating RARs");
    return nullptr;
  }

  pdcch_ul_information* alloc_ul_pdcch_common(cell_slot_resource_allocator& slot_alloc,
                                              rnti_t                        rnti,
                                              search_space_id               ss_id,
                                              aggregation_level             aggr_lvl) override
  {
    srsgnb_terminate("Common PDCCHs should not be called while allocating RARs");
    return nullptr;
  }
};

/// Helper class to initialize and store relevant objects for the test and provide helper methods.
struct test_bench {
  const bwp_id_t        bwp_id      = to_bwp_id(0);
  srslog::basic_logger& mac_logger  = srslog::fetch_basic_logger("MAC");
  srslog::basic_logger& test_logger = srslog::fetch_basic_logger("TEST");

  sched_cell_configuration_request_message cfg_msg;
  cell_configuration                       cfg;
  cell_resource_allocator                  res_grid;
  dummy_pdcch_resource_allocator           pdcch_sch;
  slot_point                               sl_tx;

  // Test bench ctor for SIB1 scheduler test use. It allows us to set single parameters.
  test_bench(subcarrier_spacing   init_bwp_scs,
             uint8_t              pdcch_config_sib1,
             uint8_t              ssb_bitmap,
             sib1_rtx_periodicity sib1_rtx_period = sib1_rtx_periodicity::ms160,
             ssb_periodicity      ssb_period      = ssb_periodicity::ms5) :
    cfg_msg{make_cell_cfg_req_for_sib_sched(init_bwp_scs, pdcch_config_sib1, ssb_bitmap, sib1_rtx_period, ssb_period)},
    cfg{cfg_msg},
    res_grid{cfg},
    sl_tx{to_numerology_value(cfg.dl_cfg_common.init_dl_bwp.generic_params.scs), 0}
  {
    res_grid.slot_indication(sl_tx);
  };

  // Test bench ctor for SSB/SIB1 scheduler collision test.
  test_bench(uint32_t           freq_arfcn,
             uint16_t           offset_to_point_A,
             uint8_t            k_ssb,
             uint8_t            ssb_bitmap,
             subcarrier_spacing init_bwp_scs,
             uint8_t            pdcch_config_sib1) :
    cfg_msg{make_cell_cfg_req_for_sib_sched(freq_arfcn,
                                            offset_to_point_A,
                                            k_ssb,
                                            ssb_bitmap,
                                            init_bwp_scs,
                                            pdcch_config_sib1)},
    cfg{cfg_msg},
    res_grid{cfg},
    sl_tx{to_numerology_value(init_bwp_scs), 0}
  {
    test_logger.set_context(0);
    mac_logger.set_context(0);
  }

  cell_slot_resource_allocator& get_slot_res_grid() { return res_grid[0]; };

  // Create default configuration and change specific parameters based on input args.
  sched_cell_configuration_request_message make_cell_cfg_req_for_sib_sched(subcarrier_spacing   init_bwp_scs,
                                                                           uint8_t              pdcch_config_sib1,
                                                                           uint8_t              ssb_bitmap,
                                                                           sib1_rtx_periodicity sib1_rtx_period,
                                                                           ssb_periodicity      ssb_period)
  {
    sched_cell_configuration_request_message msg     = make_default_sched_cell_configuration_request();
    msg.dl_cfg_common.init_dl_bwp.generic_params.scs = init_bwp_scs;
    msg.ssb_config.scs                               = init_bwp_scs;
    msg.scs_common                                   = init_bwp_scs;
    // Change Carrier parameters when SCS is 15kHz.
    if (init_bwp_scs == subcarrier_spacing::kHz15) {
      msg.dl_cfg_common.freq_info_dl.scs_carrier_list.front().carrier_bandwidth = 106;
      msg.dl_cfg_common.init_dl_bwp.generic_params.crbs                         = {
                                  0, msg.dl_cfg_common.freq_info_dl.scs_carrier_list.front().carrier_bandwidth};
    }
    // Change Carrier parameters when SCS is 30kHz.
    else if (init_bwp_scs == subcarrier_spacing::kHz30) {
      msg.dl_cfg_common.freq_info_dl.scs_carrier_list.emplace_back(
          scs_specific_carrier{0, subcarrier_spacing::kHz30, 51});
      msg.dl_cfg_common.init_dl_bwp.generic_params.crbs = {
          0, msg.dl_cfg_common.freq_info_dl.scs_carrier_list[1].carrier_bandwidth};
      // Random ARFCN that must be in FR1 and > 3GHz.
      msg.dl_carrier.arfcn          = 700000;
      msg.dl_carrier.carrier_bw_mhz = 20;
      msg.dl_carrier.nof_ant        = 1;
    }
    msg.coreset0              = (pdcch_config_sib1 >> 4U) & 0b00001111;
    msg.searchspace0          = pdcch_config_sib1 & 0b00001111;
    msg.sib1_retx_period      = sib1_rtx_period;
    msg.ssb_config.ssb_bitmap = static_cast<uint64_t>(ssb_bitmap) << static_cast<uint64_t>(56U);
    msg.ssb_config.ssb_period = ssb_period;
    return msg;
  }

  // Create default configuration and change specific parameters based on input args.
  sched_cell_configuration_request_message make_cell_cfg_req_for_sib_sched(uint32_t           freq_arfcn,
                                                                           uint16_t           offset_to_point_A,
                                                                           uint8_t            k_ssb,
                                                                           uint8_t            ssb_bitmap,
                                                                           subcarrier_spacing init_bwp_scs,
                                                                           uint8_t            pdcch_config_sib1)
  {
    sched_cell_configuration_request_message msg     = make_default_sched_cell_configuration_request();
    msg.dl_carrier.arfcn                             = freq_arfcn;
    msg.dl_cfg_common.freq_info_dl.offset_to_point_a = offset_to_point_A;
    msg.dl_cfg_common.init_dl_bwp.generic_params.scs = init_bwp_scs;
    msg.ssb_config.scs                               = init_bwp_scs;
    msg.scs_common                                   = init_bwp_scs;
    msg.ssb_config.ssb_bitmap                        = static_cast<uint64_t>(ssb_bitmap) << static_cast<uint64_t>(56U);
    msg.ssb_config.ssb_period                        = ssb_periodicity::ms10;
    msg.ssb_config.offset_to_point_A                 = ssb_offset_to_pointA{offset_to_point_A};
    msg.ssb_config.k_ssb                             = k_ssb;
    msg.sib1_retx_period                             = sib1_rtx_periodicity::ms10;
    // Change Carrier parameters when SCS is 15kHz.
    if (init_bwp_scs == subcarrier_spacing::kHz15) {
      msg.dl_cfg_common.freq_info_dl.scs_carrier_list.front().carrier_bandwidth = 106;
      msg.dl_cfg_common.init_dl_bwp.generic_params.crbs                         = {
                                  0, msg.dl_cfg_common.freq_info_dl.scs_carrier_list.front().carrier_bandwidth};
    }
    // Change Carrier parameters when SCS is 30kHz.
    else if (init_bwp_scs == subcarrier_spacing::kHz30) {
      msg.dl_cfg_common.freq_info_dl.scs_carrier_list.emplace_back(
          scs_specific_carrier{0, subcarrier_spacing::kHz30, 51});
      msg.dl_cfg_common.init_dl_bwp.generic_params.crbs = {
          0, msg.dl_cfg_common.freq_info_dl.scs_carrier_list[1].carrier_bandwidth};
    }
    msg.coreset0                  = (pdcch_config_sib1 >> 4U) & 0b00001111;
    msg.searchspace0              = pdcch_config_sib1 & 0b00001111;
    msg.dl_carrier.carrier_bw_mhz = 20;
    msg.dl_carrier.nof_ant        = 1;

    return msg;
  }

  void slot_indication()
  {
    sl_tx++;
    mac_logger.set_context(sl_tx.to_uint());
    test_logger.set_context(sl_tx.to_uint());
    test_logger.info("Starting new slot {}", sl_tx);
    res_grid.slot_indication(sl_tx);
  }

  /// Helper that tests if the PDCCH and DCI grants in the scheduled results have been filled properly.
  void assess_filled_grants()
  {
    // Test SIB_information message
    const sib_information& test_sib1 = res_grid[0].result.dl.bc.sibs.back();
    TESTASSERT_EQ(sib_information::si_indicator_type::sib1, test_sib1.si_indicator);
    TESTASSERT_EQ(SI_RNTI, test_sib1.pdsch_cfg.rnti);

    // Test PDCCH_grant and DCI
    const pdcch_dl_information* pdcch = std::find_if(res_grid[0].result.dl.dl_pdcchs.begin(),
                                                     res_grid[0].result.dl.dl_pdcchs.end(),
                                                     [](const auto& pdcch) { return pdcch.ctx.rnti == SI_RNTI; });
    TESTASSERT(pdcch != nullptr);
    TESTASSERT_EQ(dci_dl_rnti_config_type::si_f1_0, pdcch->dci.type);
    TESTASSERT_EQ(cfg_msg.sib1_mcs, pdcch->dci.si_f1_0.modulation_coding_scheme);
    TESTASSERT_EQ(cfg_msg.sib1_rv, pdcch->dci.si_f1_0.redundancy_version);
  }

  /// Tests if PRBs have been set as used in the resource grid for the current slot.
  void verify_prbs_allocation(bool got_allocated = true)
  {
    // Tests if PRBs have been allocated.
    if (got_allocated) {
      TESTASSERT(res_grid[0].dl_res_grid.used_crbs(cfg.dl_cfg_common.init_dl_bwp.generic_params, {0, 14}).any());
    } else {
      // Tests if PRBs are still unused.
      TESTASSERT(res_grid[0].dl_res_grid.used_crbs(cfg.dl_cfg_common.init_dl_bwp.generic_params, {0, 14}).none());
    }
  }
};

/// \brief Tests if the SIB1 scheduler schedules the SIB1s at the right slot n0.
/// \param[in] scs_common SCS corresponding to subCarrierSpacingCommon.
/// \param[in] sib1_n0_slots array of n0 slots; the n-th array's value is the n0 corresponding to the n-th SSB beam.
/// \param[in] pdcch_config_sib1 is the parameter (in the MIB) determining the n0 for each beam.
/// \param[in] ssb_beam_bitmap corresponds to the ssb-PositionsInBurs in the TS 38.311, with L_max = 8.
void test_sib1_scheduler(subcarrier_spacing                   scs_common,
                         std::array<unsigned, MAX_NUM_BEAMS>& sib1_n0_slots,
                         uint8_t                              pdcch_config_sib1,
                         uint8_t                              ssb_beam_bitmap)
{
  // Instantiate the test_bench and the SIB1 scheduler.
  test_bench     t_bench{scs_common, pdcch_config_sib1, ssb_beam_bitmap};
  sib1_scheduler sib1_sched{t_bench.cfg, t_bench.pdcch_sch, t_bench.cfg_msg};

  // SIB1 periodicity in slots.
  unsigned sib1_period_slots = SIB1_PERIODICITY * t_bench.sl_tx.nof_slots_per_subframe();

  // Define helper lambda to determine from ssb_beam_bitmap if the n-th SSB beam is used.
  uint64_t ssb_bitmap          = t_bench.cfg.ssb_cfg.ssb_bitmap;
  auto     nth_ssb_beam_active = [ssb_bitmap](unsigned ssb_index) {
    return (ssb_bitmap & (static_cast<uint64_t>(0b1U) << static_cast<uint64_t>(63U - ssb_index))) > 0;
  };

  // Run the test for 10000 slots.
  size_t test_length_slots = 10000;
  for (size_t sl_idx = 0; sl_idx < test_length_slots; sl_idx++) {
    // Run SIB1 scheduler.
    sib1_sched.schedule_sib1(t_bench.get_slot_res_grid(), t_bench.sl_tx);

    auto& res_slot_grid = t_bench.get_slot_res_grid();

    test_scheduler_result_consistency(t_bench.cfg, res_slot_grid.result);

    // Verify if for any active beam, the SIB1 got allocated within the proper n0 slots.
    for (size_t ssb_idx = 0; ssb_idx < MAX_NUM_BEAMS; ssb_idx++) {
      // Only check for the active slots.
      if (nth_ssb_beam_active(ssb_idx) && (sl_idx % sib1_period_slots == sib1_n0_slots[ssb_idx])) {
        // Verify that the scheduler results list contain 1 element with the SIB1 information.
        TESTASSERT_EQ(1, res_slot_grid.result.dl.bc.sibs.size());
        // Verify the PDCCH grants and DCI have been filled correctly.
        t_bench.assess_filled_grants();
        // Verify the PRBs in the res_grid are set as used.
        t_bench.verify_prbs_allocation();
      }
    }

    // Update SLOT.
    t_bench.slot_indication();
  }
}

/// \brief Tests if the SIB1 scheduler schedules SIB1s according to the correct retransmission periodicity.
///
/// This test evaluates the correct SIB1 retransmission period, which we assume it should be the maximum between the SSB
/// periodicity and the SIB1 retx periodicity set as a parameter. This is due to the fact that the SIB1 requires the SSB
/// to be decoded, meaning there is no point in scheduling SIBs more frequently than SSBs.
/// This test only evaluates the periodicity of SIB1, therefore it uses a standard set of values for the remaining
/// parameters (e.g., SCS, pdcch_config_sib1, SSB bitmap).
///
/// \param[in] sib1_rtx_period period set for the SIB1 retransmissions.
/// \param[in] ssb_period period set for the SSB.
void test_sib1_periodicity(sib1_rtx_periodicity sib1_rtx_period, ssb_periodicity ssb_period)
{
  // Instantiate the test_bench and the SIB1 scheduler.
  test_bench     t_bench{subcarrier_spacing::kHz15, 9U, 0b10000000, sib1_rtx_period, ssb_period};
  sib1_scheduler sib1_sched{t_bench.cfg, t_bench.pdcch_sch, t_bench.cfg_msg};

  // Determine the expected SIB1 retx periodicity.
  unsigned expected_sib1_period_ms;
  expected_sib1_period_ms = sib1_rtx_periodicity_to_value(sib1_rtx_period) > ssb_periodicity_to_value(ssb_period)
                                ? sib1_rtx_periodicity_to_value(sib1_rtx_period)
                                : ssb_periodicity_to_value(ssb_period);

  // SIB1 periodicity in slots.
  unsigned expected_sib1_period_slots = expected_sib1_period_ms * t_bench.sl_tx.nof_slots_per_subframe();

  // Slot (or offset) at which SIB1 PDCCH is allocated, measured as a delay compared to the slot with SSB. Specifically,
  // 5 is the offset of the SIB1 for the first beam, for searcSpaceZero = 9U, multiplexing pattern 1 (15kHz SCS, FR1);
  // as per Section 13, TS 38.213.
  const unsigned sib1_allocation_slot{5};

  // Run the test for 10000 slots.
  size_t test_length_slots = 10000;
  for (size_t sl_idx = 0; sl_idx < test_length_slots; sl_idx++) {
    // Run SIB1 scheduler.
    sib1_sched.schedule_sib1(t_bench.get_slot_res_grid(), t_bench.sl_tx);

    auto& res_slot_grid = t_bench.get_slot_res_grid();

    test_scheduler_result_consistency(t_bench.cfg, res_slot_grid.result);

    // With the SSB bitmap set 0b10000000, only the SSB and SIB1 for the 1 beams are used; we perform the check for
    // this beam.
    if ((sl_idx % expected_sib1_period_slots) == sib1_allocation_slot) {
      // Verify that the scheduler results list contain 1 element with the SIB1 information.
      TESTASSERT_EQ(1, res_slot_grid.result.dl.bc.sibs.size());
    } else {
      TESTASSERT(res_slot_grid.result.dl.bc.sibs.empty());
    }

    // Update SLOT.
    t_bench.slot_indication();
  }
}

/// \brief Tests if the any potential collision occurs between SIB1 PDCCH/PDSCH and SSB.
///
/// \param[in] freq_arfcn ARFCN of point A for DL carrier.
/// \param[in] offset_to_point_A as per TS38.211, Section 4.4.4.2.
/// \param[in] k_ssb or ssb-SubcarrierOffset, as per TS38.211, Section 7.4.3.1.
/// \param[in] ssb_bitmap is \c ssb-PositionsInBurst.inOneGroup, as per TS38.331, \c ServingCellConfigCommonSIB.
/// \param[in] scs subcarrier spacing of SSB and SCScommon.
/// \param[in] pdcch_config_sib1 is \c pdcch-ConfigSIB1, as per TS38.213, Section 13.
void test_ssb_sib1_collision(uint32_t           freq_arfcn,
                             uint16_t           offset_to_point_A,
                             uint8_t            k_ssb,
                             uint8_t            ssb_bitmap,
                             subcarrier_spacing scs,
                             uint8_t            pdcch_config_sib1)
{
  // Instantiate the test_bench and the SIB1 scheduler.
  test_bench     t_bench{freq_arfcn, offset_to_point_A, k_ssb, ssb_bitmap, scs, pdcch_config_sib1};
  sib1_scheduler sib1_sched{t_bench.cfg, t_bench.pdcch_sch, t_bench.cfg_msg};

  // Run the test for 10000 slots.
  const size_t test_length_slots = 100;
  for (size_t sl_idx = 0; sl_idx < test_length_slots; sl_idx++) {
    // Clear the SSB list of it is not empty.
    auto& ssb_list = t_bench.get_slot_res_grid().result.dl.bc.ssb_info;
    if (ssb_list.size() > 0) {
      ssb_list.clear();
    }

    // Run SSB scheduler.
    schedule_ssb(t_bench.get_slot_res_grid(), t_bench.sl_tx, t_bench.cfg);

    // Run SIB1 scheduler.
    sib1_sched.schedule_sib1(t_bench.get_slot_res_grid(), t_bench.sl_tx);

    auto& res_slot_grid = t_bench.get_slot_res_grid();

    test_scheduler_result_consistency(t_bench.cfg, res_slot_grid.result);

    test_dl_resource_grid_collisions(t_bench.cfg, res_slot_grid.result.dl);

    // Update SLOT.
    t_bench.slot_indication();
  }
}

// Test for potential collisions between SIB1 PDCCH/PDSCH and SSB.
void test_sib_1_pdsch_collisions(unsigned freq_arfcn, subcarrier_spacing scs)
{
  // NOTE: We only test 1 beam, as we don't have resource grids for multiple beams implemented yet.
  uint8_t ssb_bitmap = 0b10000000;
  // Allocate SIB1 in the same slot as SSB - searchspace0 = 0U.
  uint8_t  searchspace0 = 0U;
  unsigned nof_rbs_bpw  = scs == subcarrier_spacing::kHz15 ? 106 : 51;
  uint8_t  coreset0_max = scs == subcarrier_spacing::kHz15 ? 15 : 16;

  // Test different combinations of offsetToPointA and k_SSB.
  unsigned max_offset_to_point_A = nof_rbs_bpw - NOF_SSB_PRBS;
  // Consider a +2 increment for both offsetToPointA and k_SSB, to be compliant with 30kHz SCS.
  for (unsigned offset_to_point_A = 0; offset_to_point_A < max_offset_to_point_A; offset_to_point_A += 2) {
    for (uint8_t k_ssb_val = 0; k_ssb_val < 12; k_ssb_val += 2) {
      // Test all possible combinations of coreset0 position.
      for (uint8_t coreset0 = 0; coreset0 < coreset0_max; ++coreset0) {
        static const min_channel_bandwidth  min_channel_bw = min_channel_bandwidth::MHz5;
        pdcch_type0_css_coreset_description coreset0_param =
            pdcch_type0_css_coreset_get(min_channel_bw, scs, scs, coreset0, k_ssb_val);

        // If the Coreset 0 exceeds the BPW limit, skip this configuration.
        TESTASSERT(coreset0_param.offset >= 0, "FR2 not supported in this test");

        // CRB (with reference to SCScommon carrier) pointed to by offset_to_point_A.
        unsigned crb_ssb = scs == subcarrier_spacing::kHz15 ? offset_to_point_A : offset_to_point_A / 2;

        // If the Coreset 0 exceeds the Initial DL BPW limits, skip this configuration.
        if (crb_ssb - static_cast<unsigned>(coreset0_param.offset) +
                    static_cast<unsigned>(coreset0_param.nof_rb_coreset) >=
                nof_rbs_bpw or
            static_cast<unsigned>(coreset0_param.offset) > crb_ssb) {
          continue;
        }

        uint8_t pdcch_config_sib1 = static_cast<uint8_t>((coreset0 << 4U) + searchspace0);
        test_ssb_sib1_collision(freq_arfcn, offset_to_point_A, k_ssb_val, ssb_bitmap, scs, pdcch_config_sib1);
      }
    }
  }
}

int main()
{
  srslog::fetch_basic_logger("MAC").set_level(srslog::basic_levels::debug);
  srslog::fetch_basic_logger("TEST").set_level(srslog::basic_levels::info);
  srslog::init();

  // SCS Common: 15kHz
  // Test SIB1 scheduler for different values of searchSpaceZero (4 LSBs of pdcch_config_sib1) and for different
  // SSB_bitmaps.
  // The array sib1_slots contains the expected slots n0, at which the SIB1 is scheduled. The i-th element of the array
  // refers to the n0 for the i-th SSB's beam. The slots n0 have been pre-computed based on TS 38.213, Section 13.
  std::array<unsigned, MAX_NUM_BEAMS> sib1_slots{5, 7, 9, 11, 13, 15, 17, 19};
  // pdcch_config_sib1 = 9U => { coreset0 = 0U, searchspace0 = 9U).
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots, 9U, 0b10101010);
  // pdcch_config_sib1 = 57U => { coreset0 = 3U, searchspace0 = 9U).
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots, 57U, 0b01010101);
  // pdcch_config_sib1 = 105U => { coreset0 = 0U, searchspace0 = 9U).
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots, 105U, 0b11111111);

  std::array<unsigned, MAX_NUM_BEAMS> sib1_slots_1{2, 3, 4, 5, 6, 7, 8, 9};
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots_1, 2U, 0b10101010);
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots_1, 2U, 0b01010101);
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots_1, 2U, 0b11111111);

  std::array<unsigned, MAX_NUM_BEAMS> sib1_slots_2{7, 8, 9, 10, 11, 12, 13, 14};
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots_2, 6U, 0b10101010);
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots_2, 6U, 0b01010101);
  test_sib1_scheduler(subcarrier_spacing::kHz15, sib1_slots_2, 6U, 0b11111111);

  // SCS Common: 30kHz
  // Test SIB1 scheduler for different values of searchSpaceZero (4 LSBs of pdcch_config_sib1) and for different
  // SSB_bitmaps.
  // The array sib1_slots contains the expected slots n0, at which the SIB1 is scheduled. The i-th element of the array
  // refers to the n0 for the i-th SSB's beam. The slots n0 have been pre-computed based on TS 38.213, Section 13.
  std::array<unsigned, MAX_NUM_BEAMS> sib1_slots_3{10, 12, 14, 16, 18, 20, 22, 24};
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_3, 9U, 0b10101010);
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_3, 9U, 0b01010101);
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_3, 9U, 0b11111111);

  std::array<unsigned, MAX_NUM_BEAMS> sib1_slots_4{10, 11, 12, 13, 14, 15, 16, 17};
  // pdcch_config_sib1 = 4U => { coreset0 = 0U, searchspace0 = 4U).
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_4, 4U, 0b10101010);
  // pdcch_config_sib1 = 68U => { coreset0 = 3U, searchspace0 = 4U).
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_4, 68U, 0b01010101);
  // pdcch_config_sib1 = 100U => { coreset0 = 6U, searchspace0 = 4U).
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_4, 100U, 0b11111111);

  std::array<unsigned, MAX_NUM_BEAMS> sib1_slots_5{4, 5, 6, 7, 8, 9, 10, 11};
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_5, 12U, 0b10101010);
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_5, 12U, 0b01010101);
  test_sib1_scheduler(subcarrier_spacing::kHz30, sib1_slots_5, 12U, 0b11111111);

  // Test the SIB1 scheduler periodicity for different combinations of SIB1 retx perdiod and SSB period values.
  // This test uses a standard set of values for SCS, searchSpaceSetZero and SSB bitmap.
  test_sib1_periodicity(sib1_rtx_periodicity::ms5, ssb_periodicity::ms40);
  test_sib1_periodicity(sib1_rtx_periodicity::ms80, ssb_periodicity::ms20);
  test_sib1_periodicity(sib1_rtx_periodicity::ms10, ssb_periodicity::ms10);
  test_sib1_periodicity(sib1_rtx_periodicity::ms20, ssb_periodicity::ms80);
  test_sib1_periodicity(sib1_rtx_periodicity::ms40, ssb_periodicity::ms10);
  test_sib1_periodicity(sib1_rtx_periodicity::ms40, ssb_periodicity::ms10);
  test_sib1_periodicity(sib1_rtx_periodicity::ms160, ssb_periodicity::ms80);
  test_sib1_periodicity(sib1_rtx_periodicity::ms80, ssb_periodicity::ms160);

  // TEST SIB1/SSB collision on the resource grid. Test both SCS 15kHz and SCS 30kHz.
  // SCS 15kHz.
  subcarrier_spacing scs = subcarrier_spacing::kHz15;
  // This can be any frequency such that the DL band has SSB SCS 15kHz (case A, in this case).
  uint32_t freq_arfcn = 500000;

  test_sib_1_pdsch_collisions(freq_arfcn, scs);

  // SCS 30kHz.
  scs = subcarrier_spacing::kHz30;
  // This can be any frequency such that the DL band has SSB SCS 30kHz (case B, in this case).
  freq_arfcn = 176000;

  test_sib_1_pdsch_collisions(freq_arfcn, scs);

  return 0;
}