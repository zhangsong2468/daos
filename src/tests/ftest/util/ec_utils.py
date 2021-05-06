#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
import time

from nvme_utils import ServerFillUp
from daos_utils import DaosCommand
from test_utils_container import TestContainer
from pydaos.raw import DaosApiError
from test_utils_pool import TestPool
from apricot import TestWithServers

def get_data_parity_number(oclass):
    """Return EC Object Data and Parity count.
    Args:
        oclass(string): EC Object type.
    return:
        result[list]: Data and Parity numbers from object type
    """
    if 'EC' not in oclass:
        log.error("Provide EC Object type only and not %s", str(oclass))
        return 0

    tmp = re.findall(r'\d+', oclass)
    return {'data': tmp[0], 'parity': tmp[1]}

class ErasureCodeIor(ServerFillUp):
    # pylint: disable=too-many-ancestors
    """

    Class to used for EC testing.
    It will get the object types from yaml file write the IOR data set with
    IOR.

    """

    def __init__(self, *args, **kwargs):
        """Initialize a ServerFillUp object."""
        super().__init__(*args, **kwargs)
        self.server_count = None
        self.ec_container = None
        self.cont_uuid = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Fail IOR test in case of Warnings
        self.fail_on_warning = True
        self.server_count = len(self.hostlist_servers) * 2
        # Create the Pool
        self.create_pool_max_size()
        self.update_ior_cmd_with_pool()

    def setup_ior(self):
        """Get the IOR parameters from yaml file for Array data type"""
        self.obj_class = self.params.get("dfs_oclass",
                                         '/run/ior/objectclass/*')
        self.ior_chu_trs_blk_size = self.params.get(
            "chunk_block_transfer_sizes", '/run/ior/*')

    def setup_single_type(self):
        """Get the other parameters from yaml file for single data type"""
        self.obj_class = self.params.get("dfs_oclass",
                                         '/run/objectclass/*')
        self.singledata_set = self.params.get(
            "single_data_set", '/run/container/*')

    def ec_container_create(self, oclass):
        """Create the container for EC object"""
        # Get container params
        self.ec_container = TestContainer(
            self.pool, daos_command=DaosCommand(self.bin))
        self.ec_container.get_params(self)
        self.ec_container.oclass.update(oclass)
        # update object class for container create, if supplied
        # explicitly.
        ec_object = get_data_parity_number(oclass)
        self.ec_container.properties.update("rf:{}".format(
            ec_object['parity']))

        # create container
        self.ec_container.create()

    def pre_tear_down(self):
        """Tear down steps to run before tearDown().

        Returns:
            list: a list of error strings to report at the end of tearDown().

        """
        error_list = []
        for uuid in self.cont_uuid:
            print("----SAMIR- TearDown - Destorying {}\n".format(uuid))
            try:
                self.ec_container.uuid = uuid
                self.ec_container.destroy()
            except (DaosApiError) as error:
                self.test_log.info("  {}".format(error))
                error_list.append("Error destroying container: {}"
                                  .format(error))
        return error_list

    def ior_param_update(self, oclass, sizes):
        """Update the IOR command parameters.

        Args:
            oclass(list): list of the obj class to use with IOR
            sizes(list): Update Transfer, Chunk and Block sizes
        """
        self.ior_cmd.block_size.update(sizes[1])
        self.ior_cmd.transfer_size.update(sizes[2])
        self.ior_cmd.dfs_oclass.update(oclass[0])
        self.ior_cmd.dfs_dir_oclass.update(oclass[0])
        self.ior_cmd.dfs_chunk.update(sizes[0])

    def single_type_param_update(self, single_value):
        """Update the IOR command parameters.

        Args:
            oclass(list): list of the obj class to use with IOR
            sizes(list): Update Transfer, Chunk and Block sizes
        """
        self.ec_container.object_qty.update(single_value[0])
        self.ec_container.record_qty.update(single_value[1])
        self.ec_container.akey_size.update(single_value[2])
        self.ec_container.dkey_size.update(single_value[3])
        self.ec_container.data_size.update(single_value[4])

    def ior_write_dataset(self):
        """Write IOR data set with different EC object and different sizes."""
        for oclass in self.obj_class:
            for sizes in self.ior_chu_trs_blk_size:
                # Skip the object type if server count does not meet the minimum
                # EC object server count
                if oclass[1] > self.server_count:
                    continue
                self.ior_param_update(oclass, sizes)

                # Create the new container with correct redundancy factor
                # for EC object type
                self.ec_container_create(oclass[0])
                self.update_ior_cmd_with_pool(create_cont=False)
                # Start IOR Write
                self.container.uuid = self.ec_container.uuid
                self.start_ior_load(operation="WriteRead", percent=1,
                                    create_cont=False)
                self.cont_uuid.append(self.ior_cmd.dfs_cont.value)

    def ior_read_dataset(self, parity=1):
        """Read IOR data and verify for different EC object and different sizes.

        Args:
           data_parity(str): object parity type for reading, default All.
        """
        con_count = 0
        for oclass in self.obj_class:
            for sizes in self.ior_chu_trs_blk_size:
                # Skip the object type if server count does not meet the minimum
                # EC object server count
                if oclass[1] > self.server_count:
                    continue
                parity_set = "P{}".format(parity)
                # Read the requested data+parity data set only
                if parity != 1 and parity_set not in oclass[0]:
                    print("Skipping Read as object type is {}"
                          .format(oclass[0]))
                    con_count += 1
                    continue
                self.ior_param_update(oclass, sizes)
                self.container.uuid = self.cont_uuid[con_count]
                # Start IOR Read
                self.start_ior_load(operation='Read', percent=1,
                                    create_cont=False)
                con_count += 1

    def write_single_type_dataset(self):
        """Write IOR data set with different EC object and different sizes."""
        print("---------SAMIR---- write_objects \n")
        for oclass in self.obj_class:
            for sizes in self.singledata_set:
                # Skip the object type if server count does not meet the minimum
                # EC object server count
                if oclass[1] > self.server_count:
                    continue
                # Create the new container with correct redundancy factor
                # for EC object type
                self.ec_container_create(oclass[0])            

                # Start Writing data
                self.single_type_param_update(sizes)
                self.container.uuid = self.ec_container.uuid
                self.ec_container.write_objects()
                self.cont_uuid.append(self.ec_container.uuid)
                
        print("---------SAMIR---- write_objects Done\n")

    def read_single_type_dataset(self, parity=1):
        """Read IOR data and verify for different EC object and different sizes.

        Args:
           data_parity(str): object parity type for reading, default All.
        """
        print("---------SAMIR---- read_objects \n")
        con_count = 0
        for oclass in self.obj_class:
            for sizes in  self.singledata_set:
                # Skip the object type if server count does not meet the minimum
                # EC object server count
                if oclass[1] > self.server_count:
                    continue
                parity_set = "P{}".format(parity)
                # Read the requested data+parity data set only
                if parity != 1 and parity_set not in oclass[0]:
                    print("Skipping Read as object type is {}"
                          .format(oclass[0]))
                    con_count += 1
                    continue

                self.ec_container.uuid = self.cont_uuid[con_count]
                # Start Reading data
                self.ec_container.read_objects()
                con_count += 1
                
        print("---------SAMIR---- read_objects Done\n")
