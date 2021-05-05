#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time
from ec_utils import ErasureCodeSingle
from apricot import skipForTicket

class EcDisabledRebuildSingle(ErasureCodeSingle):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: To validate Erasure code object data after killing
                            single server when pool rebuild is off.
    :avocado: recursive
    """
    def test_ec_degrade_single_value(self):
        """Jira ID: DAOS-7314.

        Test Description: Test Erasure code object for single type.
        Use Case: Create the pool, disabled rebuild, Write data with single
                  type with EC object type class. kill single server,
                  read data and verified.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_disabled_rebuild
        :avocado: tags=ec_disabled_rebuild_single

        """
        # Disabled pool Rebuild
        self.pool.set_property("self_heal", "exclude")
        time.sleep(30)
        print("---------SAMIR---- write_objects \n")
        self.ec_container_create()
        self.ec_container.write_objects(obj_class="OC_EC_2P1G1")
        self.pool.display_pool_daos_space("After writes")
        time.sleep(60)
        print("---------SAMIR---- read_objects \n")
        self.ec_container.read_objects()
        time.sleep(60)
        self.pool.display_pool_daos_space("After Read")
        self.ec_container_destroy()
        # Write the IOR data set with given all the EC object type
        ## self.write_single_type_dataset()

        # Kill the last server rank and wait for 20 seconds, Rebuild is disabled
        # so data should not be rebuild
        ## self.server_managers[0].stop_ranks([self.server_count - 1], self.d_log, force=True)
        ## time.sleep(20)

        # Read IOR data and verify for different EC object and different sizes
        # written before killing the single server
        ## self.write_single_type_dataset()

        # Kill the another server rank and wait for 20 seconds,Rebuild will
        # not happens because i's disabled.Read/verify data with Parity 2.
        ## self.server_managers[0].stop_ranks([self.server_count - 2], self.d_log, force=True)
        ## time.sleep(20)

        # Read IOR data and verify for different EC object and different sizes
        # written before killing the single server
        # self.ior_read_dataset(parity=2)
        