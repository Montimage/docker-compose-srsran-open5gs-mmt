/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "dmrs_pdsch_processor_test_data.h"
#include "srsgnb/phy/upper/signal_processors/signal_processor_factories.h"

using namespace srsgnb;

int main()
{
  std::shared_ptr<pseudo_random_generator_factory> prg_factory = create_pseudo_random_generator_sw_factory();
  TESTASSERT(prg_factory);

  std::shared_ptr<dmrs_pdsch_processor_factory> dmrs_processor_factory =
      create_dmrs_pdsch_processor_factory_sw(prg_factory);
  TESTASSERT(dmrs_processor_factory);

  // Create DMRS-PDSCH processor.
  std::unique_ptr<dmrs_pdsch_processor> dmrs_pdsch = dmrs_processor_factory->create();
  TESTASSERT(dmrs_pdsch);

  for (const test_case_t& test_case : dmrs_pdsch_processor_test_data) {
    // Create resource grid.
    resource_grid_writer_spy grid;

    // Map DMRS-PDSCH using the test case arguments.
    dmrs_pdsch->map(grid, test_case.config);

    // Load output golden data.
    const std::vector<resource_grid_writer_spy::expected_entry_t> testvector_symbols = test_case.symbols.read();

    // Assert resource grid entries.
    grid.assert_entries(testvector_symbols);
  }

  return 0;
}
