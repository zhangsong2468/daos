# DAOS Internals

The purpose of this document is to describe the internal code structure and
major algorithms used by DAOS. It assumes prior knowledge of
the <a href="/doc/overview/storage.md">DAOS storage model</a>
and <a href="/doc/overview/terminology.md">acronyms</a>.
This document contains the following sections:

- <a href="#1">DAOS Components</a>
    - <a href="#11">DAOS System</a>
    - <a href="#12">Client APIs, Tools and I/O Middleware</a>
    - <a href="#13">Agent</a>
- <a href="#2">Network Transport and Communications</a>
    - <a href="#21">gRPC and Protocol Buffers</a>
    - <a href="#22">dRPC</a>
    - <a href="#23">CART</a>
- <a href="#3">DAOS Layering and Services</a>
    - <a href="#31">Architecture</a>
    - <a href="#32">Code Structure</a>
    - <a href="#33">Infrastructure Libraries</a>
    - <a href="#34">DAOS Services</a>
- <a href="#4">Software compatibility</a>
    - <a href="#41">Protocol Compatibility</a>
    - <a href="#42">PM Schema Compatibility and Upgrade</a>

<a id="1"></a>
## DAOS Components

As illustrated in the diagram below, a DAOS installation involves several
components that can be either colocated or distributed.
The DAOS software-defined storage (SDS) framework relies on two different
communication channels: an out-of-band TCP/IP network for management and
a high-performant fabric for data access. In practice, the same network
can be used for both management and data access. IP over fabric can also
be used as the management network.

![DAOS SDS Components](/doc/graph/system_architecture.png)

<a id="11"></a>
### DAOS System

A DAOS server is a multi-tenant daemon running on a Linux instance
(i.e. physical node, VM or container) and managing the locally-attached
SCM and NVM storage allocated to DAOS. It listens to a management port, addressed
by an IP address and a TCP port number, plus one or more fabric endpoints,
addressed by network URIs. The DAOS server is configured through a YAML
file (`/etc/daos/daos_server.yml`, or a different path provided on the
command line). Starting and stopping the DAOS server
can be integrated with different daemon management or
orchestration frameworks (e.g. a systemd script, a Kubernetes service or
even via a parallel launcher like pdsh or srun).

A DAOS system is identified by a system name and consists of a set of
DAOS servers connected to the same fabric. Two different systems comprise
two disjoint sets of servers and do not coordinate with each other.
DAOS pools cannot span across multiple systems.

Internally, a DAOS server is composed of multiple daemon processes. The first
one to be started is the <a href="control/README.md">control plane</a>
(binary named `daos_server`) which is responsible for parsing
the configuration file, provisionning storage and eventually starting and
monitoring one or multiple instances of the <a href="engine/README.md">data plane</a>
(binary named `daos_engine`).
The control plane is written in Go and implements the DAOS management API
over the gRPC framework that provides a secured out-of-band channel to
administrate a DAOS system. The number of data plane instances to be started
by each server as well as the storage, CPU and fabric interface affinity can
be configured through the `daos_server.yml` YAML configuration file.

The data plane is a multi-threaded process written in C that runs the DAOS
storage engine. It processes incoming metadata and I/O requests though the
CART communication middleware and accesses local NVM storage via the PMDK
(for storage-class memory, aka SCM) and SPDK (for NVMe SSDs) libraries.
The data plane relies on Argobots for event-based parallel processing and
exports multiple targets that can be independently addressed via the fabric.
Each data plane instance is assigned a unique rank inside a DAOS system.

The control plane and data plane processes communicate locally through Unix
Domain Sockets and a custom lightweight protocol called dRPC.

For further reading:
- <a href="control/README.md">DAOS control plane (daos_server)</a>
- <a href="engine/README.md">DAOS data plane (daos_engine)</a>

<a id="12"></a>
### Client APIs, Tools and I/O Middleware

Applications, users and administrators can interact with a DAOS system
through two different client APIs.

The DAOS management Go package allows to administrate a DAOS system
from any nodes that can communicate with the DAOS servers through the
out-of-band management channel. This API is reserved for the DAOS system
administrators who are authenticated through a specific certificate.
The DAOS management API is intended to be integrated with different
vendor-specific storage management or open-source orchestration frameworks.
A CLI tool called `dmg` is built over the DAOS management API.
For further reading on the management API and the `dmg` tool:
- <a href="https://godoc.org/github.com/daos-stack/daos/src/control/client">DAOS management Go package</a>
- <a href="control/cmd/dmg/README.md">DAOS Management tool (aka dmg)</a>

The DAOS library (`libdaos`) implements the DAOS storage model and is
primarily targeted at application and I/O middleware developers who want
to store datasets into DAOS containers. It can be used from any nodes
connected to the fabric used by the targeted DAOS system. The application
process is authenticated via the DAOS agent (see next section).
The API exported by `libdaos` is commonly called the DAOS API (in contrast
to the DAOS management API) and allows to manage containers and access DAOS
objects through different interfaces (e.g. key-value store or array API).
The `libdfs` library emulates POSIX file and directory abstractions over
`libdaos` and provides a smooth migration path for applications that require
a POSIX namespace. For further reading on `libdaos`, bindings for different
programming languages and `libdfs`:
- <a href="client/api/README.md">DAOS Library (`libdaos`)</a> and <a href="client/array/README.md">array interface</a> and <a href="client/kv/README.md">KV interface</a> built on top of the native DAOS API</a>
- <a href="src/client/pydaos/raw/README.md">Python API bindings</a>
- <a href="https://github.com/daos-stack/go-daos">Go bindings</a> and <a href="https://godoc.org/github.com/daos-stack/go-daos/pkg/daos">API documentation</a>
- <a href="client/dfs/README.md">POSIX File & Directory Emulation (`libdfs`)</a>

The `libdaos` and `libdfs` libraries provide the foundation to support
domain-specific data formats like HDF5 and Apache Arrow. For further reading
on I/O middleware integration, please check the following external references:
- <a href="https://bitbucket.hdfgroup.org/projects/HDFFV/repos/hdf5/browse?at=refs%2Fheads%2Fhdf5_daosm">DAOS VOL connector for HDF5</a>
- <a href="https://github.com/daos-stack/mpich/tree/daos_adio">ROMIO DAOS ADIO driver for MPI-IO</a>

<a id="13"></a>
### Agent

The <a href="control/cmd/daos_agent/README.md">DAOS agent</a> is a daemon
residing on the client nodes. It interacts with the DAOS client library
through dRPC to authenticate the application process. It is a trusted entity
that can sign the DAOS Client credentials using local certificates.
The DAOS agent can support different authentication frameworks and uses a
Unix Domain Socket to communicate with the client library.
The DAOS agent is written in Go and communicates through gRPC with the
control plane component of each DAOS server to provide DAOS system
membership information to the client library and to support pool listing.

<a id="2"></a>
## Network Transport and Communications

As introduced in the previous section, DAOS uses three different
communication channels.

<a id="21"></a>
### gRPC and Protocol Buffers

gRPC provides a bi-directional secured channel for DAOS management.
It relies on TLS/SSL to authenticate the administrator role and the servers.
Protocol buffers are used for RPC serialization and all proto files are
located in the [proto](proto) directory.

<a id="22"></a>
### dRPC

dRPC is communication channel built over Unix Domain Socket that is used
for inter-process communications.
It provides both a [C](common/README.md#dRPC-C-API) and [Go](control/drpc/README.md)
interface to support interactions between:
- the `daos_agent` and `libdaos` for application process authentication
- the `daos_server` (control plane) and the `daos_engine` (data plane) daemons
Like gRPC, RPC are serialized via protocol buffers.

<a id="23"></a>
### CART

[CART](https://github.com/daos-stack/cart) is a userspace function shipping
library that provides low-latency high-bandwidth communications for the DAOS
data plane. It supports RDMA capabilities and scalable collective operations.
CART is built over [Mercury](https://github.com/mercury-hpc/mercury) and
[libfabric](https://ofiwg.github.io/libfabric/).
The CART library is used for all communications between
`libdaos` and `daos_engine` instances.

<a id="3"></a>
## DAOS Layering and Services

<a id="31"></a>
### Architecture

As shown in the diagram below, the DAOS stack is structured as a collection
of storage services over a client/server architecture.
Examples of DAOS services are the pool, container, object and rebuild services.

![DAOS Internal Services & Libraries](/doc/graph/services.png)

A DAOS service can be spread across the control and data planes and
communicate internally through dRPC. Most services have client and server
components that can synchronize through gRPC or CART. Cross-service
communications are always done through direct API calls.
Those function calls can be invoked across either the client or server
component of the services. While each DAOS service is designed to be fairly
autonomous and isolated, some are more tightly coupled than others.
That is typically the case of the rebuild service that needs to interact
closely with the pool, container and object services to restore data
redundancy after a DAOS server failure.

While the service-based architecture offers flexibility and extensibility,
it is combined with a set of infrastucture libraries that provide a rich
software ecosystem (e.g. communications, persistent storage access,
asynchronous task execution with dependency graph, accelerator support, ...)
accessible to all the DAOS services.

<a id="32"></a>
### Source Code Structure

Each infrastructure library and service is allocated a dedicated directory
under `src/`. The client and server components of a service are stored in
separate files. Functions that are part of the client component are prefixed
with `dc\_` (stands for DAOS Client) whereas server-side functions use the
`ds\_` prefix (stands for DAOS Server).
The protocol and RPC format used between the client and server components
is usually defined in a header file named `rpc.h`.

All the Go code executed in context of the control plane is located under
`src/control`. Management and security are the services spread across the
control (Go language) and data (C language) planes and communicating
internally through dRPC.

Headers for the official DAOS API exposed to the end user (i.e. I/O
middleware or application developers) are under `src/include` and use the
`daos\_` prefix. Each infrastructure library exports an API that is
available under `src/include/daos` and can be used by any services.
The client-side API (with `dc\_` prefix) exported by a given service
is also stored under `src/include/daos` whereas the server-side
interfaces (with `ds\_` prefix) are under `src/include/daos_srv`.

<a id="33"></a>
### Infrastructure Libraries

The GURT and common DAOS (i.e. `libdaos\_common`) libraries provide logging,
debugging and common data structures (e.g. hash table, btree, ...)
to the DAOS services.

Local NVM storage is managed by the Versioning Object Store (VOS) and
blob I/O (BIO) libraries. VOS implements the persistent index in SCM
whereas BIO is responsible for storing application data in either NVMe SSD
or SCM depending on the allocation strategy. The VEA layer is integrated
into VOS and manages block allocation on NVMe SSDs.

DAOS objects are distributed across multiple targets for both performance
(i.e. sharding) and resilience (i.e. replication or erasure code).
The placement library implements different algorithms (e.g. ring-based
placement, jump consistent hash, ...) to generate the layout of an
object from the list of targets and the object identifier.

The replicated service (RSVC) library finally provides some common code
to support fault tolerance. This is used by the pool, container & management
services in conjunction with the RDB library that implements a replicated
key-value store over Raft.

For further reading on those infrastructure libraries, please see:
- <a href="common/README.md">Common Library</a>
- <a href="vos/README.md">Versioning Object Store (VOS)</a>
- <a href="bio/README.md">Blob I/O (BIO)</a>
- <a href="placement/README.md">Algorithmic object placement</a>
- <a href="rdb/README.md">Replicated database (RDB)</a>
- <a href="rsvc/README.md">Replicated service framework (RSVC)</a>

<a id="34"></a>
### DAOS Services

The diagram below shows the internal layering of the DAOS services and
interactions with the different libraries mentioned above.
![DAOS Internal Layering](/doc/graph/layering.png)

Vertical boxes represent DAOS services whereas horizontal ones are for
infrastructure libraries.

For further reading on the internals of each service:
- <a href="pool/README.md">Pool service</a>
- <a href="container/README.md">Container service</a>
- <a href="object/README.md">Key-array object service</a>
- <a href="rebuild/README.md">Self-healing (aka rebuild)</a>
- <a href="security/README.md">Security</a>

<a id="4"></a>
## Software Compatibility

Interoperability in DAOS is handled via protocol and schema versioning for
persistent data structures.

<a id="41"></a>
### Protocol Compatibility

Limited protocol interoperability is to be provided by the DAOS storage stack.
Version compatibility checks will be performed to verify that:

* All targets in the same pool run the same protocol version.
* Client libraries linked with the application may be up to one
  protocol version older than the targets.

If a protocol version mismatch is detected among storage targets in the same
pool, the entire DAOS system will fail to start up and will report failure
to the control API. Similarly, connection from clients running a protocol
version incompatible with the targets will return an error.

<a id="42"></a>
### PM Schema Compatibility and Upgrade

The schema of persistent data structures may evolve from time to time to
fix bugs, add new optimizations or support new features. To that end,
the persistent data structures support schema versioning.

Upgrading the schema version is not done automatically and must be initiated
by the administrator. A dedicated upgrade tool will be provided to upgrade
the schema version to the latest one. All targets in the same pool must have
the same schema version. Version checks are performed at system initialization
time to enforce this constraint.

To limit the validation matrix, each new DAOS release will be published
with a list of supported schema versions. To run with the new DAOS release,
administrators will then need to upgrade the DAOS system to one of the
supported schema version. New target will always be reformatted with the
latest version. This versioning schema only applies to data structure
stored in persistent memory and not to block storage that only stores
user data with no metadata.


=============================================================================


# Blob I/O

The Blob I/O (BIO) module was implemented for issuing I/O over NVMe SSDs. The BIO module covers NVMe SSD support, faulty device detection, device health monitoring, NVMe SSD hot plug functionality, and also SSD identification with the use of Intel VMD devices.

This document contains the following sections:

- <a href="#1">NVMe SSD Support</a>
    - <a href="#2">Storage Performance Development Kit (SPDK)</a>
    - <a href="#3">Per-Server Metadata Management (SMD)</a>
    - <a href="#4">DMA Buffer Management</a>
- <a href="#5">NVMe Threading Model</a>
- <a href="#6">Device Health Monitoring</a>
- <a href="#7">Faulty Device Detection (SSD Eviction)</a>
- <a href="#8">NVMe SSD Hot Plug</a>
- <a href="#9">SSD Identification</a>
    - <a href="#10">Intel Volume Management Device (VMD)
- <a href="#11">Device States</a>
- <a href="#12">User Interfaces</a>

<a id="1"></a>
## NVMe SSD Support
The DAOS service has two tiers of storage: Storage Class Memory (SCM) for byte-granular application data and metadata, and NVMe for bulk application data. Similar to how PMDK is currently used to facilitate access to SCM, the Storage Performance Development Kit (SPDK) is used to provide seamless and efficient access to NVMe SSDs. DAOS storage allocations can occur on either SCM by using a PMDK pmemobj pool, or on NVMe, using an SPDK blob. All local server metadata will be stored in a per-server pmemobj pool on SCM and will include all current and relevant NVMe device, pool, and xstream mapping information. Background aggregation allows for data migration from SCM to an NVMe SSD by coalescing smaller data records into a larger one. The DAOS control plane handles all SSD configuration, and the DAOS data plane handles all allocations through SPDK, with finer block allocations using the in-house Versioned Extent Allocator (VEA).

<a id="2"></a>
### Storage Performance Development Kit (SPDK)
SPDK is an open source C library that when used in a storage application, can provide a significant performance increase of more than 7X over the standard NVMe kernel driver. SPDK's high performance can mainly be attributed to the user space NVMe driver, eliminating all syscalls and enabling zero-copy access from the application. In SPDK, the hardware is polled for completions as opposed to relying on interrupts, lowering both total latency and latency variance. SPDK also offers a block device layer called bdev which sits immediately above the device drivers like in a traditional kernel storage stack. This module offers pluggable module APIs for implementing block devices that interface with different types of block storage devices. This includes driver modules for NVMe, Malloc (ramdisk), Linux AIO, Ceph RBD, and others.

![/doc/graph/Fig_065.png](/doc/graph/Fig_065.png "SPDK Software Stack")

#### SPDK NVMe Driver
The NVMe driver is a C library linked to a storage application providing direct, zero-copy data transfer to and from NVMe SSDs. Other benefits of the SPDK NVMe driver are that it is entirely in user space, operates in polled-mode vs. interrupt-dependent, is asynchronous and lock-less.
#### SPDK Block Device Layer (bdev)
The bdev directory contains a block device abstraction layer used to translate from a common block protocol to specific protocols of backend devices, such as NVMe. Additionally, this layer provides automatic queuing of I/O requests in response to certain conditions, lock-less sending of queues, device configuration and reset support, and I/O timeout trafficking.
#### SPDK Blobstore
The blobstore is a block allocator for a higher-level storage service. The allocated blocks are termed 'blobs' within SPDK. Blobs are designed to be large (at least hundreds of KB), and therefore another allocator is needed in addition to the blobstore to provide efficient small block allocation for the DAOS service. The blobstore provides asynchronous, un-cached, and parallel blob read and write interfaces

### SPDK Integration
The BIO module relies on the SPDK API to initialize/finalize the SPDK environment on the DAOS server start/shutdown. The DAOS storage model is integrated with SPDK by the following:

* Management of SPDK blobstores and blobs:
NVMe SSDs are assigned to each DAOS server xstream. SPDK blobstores are created on each NVMe SSD. SPDK blobs are created and attached to each per-xstream VOS pool.
* Association of SPDK I/O channels with DAOS server xstreams:
Once SPDK I/O channels are properly associated to the corresponding device, NVMe hardware completion pollers are integrated into server polling ULTs.

<a id="3"></a>
## Per-Server Metadata Management (SMD)
One of the major subcomponenets of the BIO module is per-server metadata management. The SMD submodule consists of a PMDK pmemobj pool stored on SCM used to track each DAOS server's local metadata.

Currently, the persistent metadata tables tracked are :
  - **NVMe Device Table**: NVMe SSD to DAOS server xstream mapping (local PCIe attached NVMe SSDs are assigned to different server xstreams to avoid hardware contention). A persistent device state is also stored (supported device states are: NORMAL and FAULTY).
  - **NVMe Pool Table**: NVMe SSD, DAOS server xstream, and SPDK blob ID mapping (SPDK blob to VOS pool:xstream mapping). Blob size is also stored along with the SPDK blob ID in order to support creating new blobs on a new device in the case of NVMe device hotplug.

On DAOS server start, these tables are loaded from persistent memory and used to initialize new, and load any previous blobstores and blobs. Also, there is potential to expand this module to support other non-NVMe related metadata in the future.

Useful admin commands to query per-server metadata:
   <a href="#80">dmg storage query (list-devices | list-pools)</a> [used to query both SMD device table and pool table]

<a id="4"></a>
## DMA Buffer Management
BIO internally manages a per-xstream DMA safe buffer for SPDK DMA transfer over NVMe SSDs. The buffer is allocated using the SPDK memory allocation API and can dynamically grow on demand. This buffer also acts as an intermediate buffer for RDMA over NVMe SSDs, meaning on DAOS bulk update, client data will be RDMA transferred to this buffer first, then the SPDK blob I/O interface will be called to start local DMA transfer from the buffer directly to NVMe SSD. On DAOS bulk fetch, data present on the NVMe SSD will be DMA transferred to this buffer first, and then RDMA transferred to the client.

<a id="5"></a>
## NVMe Threading Model
  - Device Owner Xstream: In the case there is no direct 1:1 mapping of VOS XStream to NVMe SSD, the VOS xstream that first opens the SPDK blobstore will be named the 'Device Owner'. The Device Owner Xstream is responsible for maintaining and updating the blobstore health data, handling device state transitions, and also media error events. All non-owner xstreams will forward events to the device owner.
  - Init Xstream: The first started VOS xstream is termed the 'Init Xstream'. The init xstream is responsible for initializing and finalizing the SPDK bdev, registering the SPDK hotplug poller, handling and periodically checking for new NVMe SSD hot remove and hotplug events, and handling all VMD LED device events.

![/doc/graph/NVME_Threading_Model_Final](/doc/graph/NVME_Threading_Model_Final.PNG "NVMe Threading Model")

Above is a diagram of the current NVMe threading model. The 'Device Owner' xstream is responsible for all faulty device and device reintegration callbacks, as well as updating device health data. The 'Init' xstream is responsible for registering the SPDK hotplug poller and maintaining the current device list of SPDK bdevs as well as evicted and unplugged devices. Any device metadata operations or media error events that do not occur on either of these two xstreams will be forwarded to the appropriate xstream using the SPDK event framework for lockless inter-thread communication. All xstreams will periodically poll for I/O statistics (if enabled in server config), but only the device owner xstream will poll for device events, making necessary state transitions, and update device health stats, and the init xstream will poll for any device removal or device hot plug events.

<a id="6"></a>
## Device Health Monitoring
The device owner xstream is responsible for maintaining anf updating all device health data and all media error events as apart of the device health monitoring feature. Device health data consists of raw SSD health stats queried via SPDK admin APIs and in-memory health data. The raw SSD health stats returned include useful and critical data to determine the current health of the device, such as temperature, power on duration, unsafe shutdowns, critical warnings, etc. The in-memory health data contains a subset of the raw SSD health stats, in addition to I/O error (read/write/unmap) and checksum error counters that are updated when a media error event occurs on a device and stored in-memory.

The DAOS data plane will monitor NVMe SSDs every 60 seconds, including updating the health stats with current values, checking current device states, and making any necessary blobstore/device state transitions. Once a FAULTY state transition has occurred, the monitoring period will be reduced to 10 seconds to allow for quicker transitions and finer-grained monitoring until the device is fully evicted.

 Useful admin commands to query device health:
  - <a href="#81">dmg storage query (device-health | target-health)</a> [used to query SSD health stats]

While monitoring this health data, an admin can now make the determination to manually evict a faulty device. This data will also be used to set the faulty device criteria for automatic SSD eviction (available in a future release).

<a id="7"></a>
## Faulty Device Detection (SSD Eviction)
Faulty device detection and reaction can be referred to as NVMe SSD eviction. This involves all affected pool targets being marked as down and the rebuild of all affected pool targets being automatically triggered. A persistent device state is maintained in SMD and the device state is updated from NORMAL to FAULTY upon SSD eviction. The faulty device reaction will involve various SPDK cleanup, including all I/O channels released, SPDK allocations (termed 'blobs') closed, and the SPDK blobstore created on the NVMe SSD unloaded. Currently only manual SSD eviction is supported, and a future release will support automatic SSD eviction.

 Useful admin commands to manually evict an NVMe SSD:
  - <a href="#82">dmg storage set nvme-faulty</a> [used to manually set an NVMe SSD to FAULTY (ie evict the device)]

<a id="8"></a>
## NVMe SSD Hot Plug

**Full NVMe hot plug capability will be available and supported in DAOS 2.0 release. Use is currently intended for testing only and is not supported for production.**

The NVMe hot plug feature includes device removal (an NVMe hot remove event) and device reintegration (an NVMe hotplug event) when a faulty device is replaced with a new device.

For device removal, if the device is a faulty or previously evicted device, then nothing further would be done when the device is removed. The device state would be displayed as UNPLUGGED. If a healthy device that is currently in use by DAOS is removed, then all SPDK memory stubs would be deconstructed, and the device state would also display as UNPLUGGED.

For device reintegration, if a new device is plugged to replace a faulty device, the admin would need to issue a device replacement command. All SPDK in-memory stubs would be created and all affected pool targets automatically reintegrated on the new device. The device state would be displayed as NEW initially and NORMAL after the replacement event occurred. If a faulty device or previously evicted device is re-plugged, the device will remain evicted, and the device state would display EVICTED. If a faulty device is desired to be reused (NOTE: this is not advised, mainly used for testing purposes), the admin can run the same device replacement command setting the new and old device IDs to be the same device ID. Reintegration will not occur on the device, as DAOS does not currently support incremental reintegration.

NVMe hot plug with Intel VMD devices is currently not supported in this release, but will be supported in a future release.

 Useful admin commands to replace an evicted device:
  - <a href="#83">dmg storage replace nvme</a> [used to replace an evicted device with a new device]
  - <a href="#84">dmg storage replace nvme</a> [used to bring an evicted device back online (without reintegration)]

<a id="9"></a>
## SSD Identification
The SSD identification feature is a way to quickly and visually locate a device. It requires the use of Intel VMD, which needs to be physically available on the hardware as well as enabled in the system BIOS. The feature supports two LED events: locating a healthy device and locating an evicted device.

<a id="10"></a>
### Intel Volume Management Device (VMD)
Intel VMD is a technology embedded in the processor silicon that aggregates the NVMe PCIe SSDs attached to its root port, acting as an HBA does for SATA and SAS. Currently, PCIe storage lacks a standardized method to blink LEDs and indicated the status of a device. Intel VMD, along with NVMe, provides this support for LED management.

![/doc/graph/Intel_VMD.png](/doc/graph/Intel_VMD.png "Intel VMD Technology")
Intel VMD places a control point in the PCIe root complex of the servers, meaning that NVMe drives can be hot-swapped, and the status LED is always reliable.

![/doc/graph/VMD_Amber_LED.png](/doc/graph/VMD_Amber_LED.png "Status Amber VMD LED")
The Amber LED (status LED) is what VMD provides. It represents the LED coming from the slot on the backplane. The Green LED is the activity LED.

The status LED on the VMD device has four states: OFF, FAULT, REBUILD, and IDENTIFY. These are communicated by blinking patterns specified in the IBPI standard (SFF-8489).
![/doc/graph/VMD_LED_states.png](/doc/graph/VMD_LED_states.png "Status LED states")

#### Locate a Health Device
Upon issuing a device identify command with a specified device ID, an admin now can quickly identify a device in question. The status LED on the VMD device would be set to an IDENTIFY state, represented by a quick, 4Hz blinking amber light. The device would quickly blink by default for 60 seconds and then return to the default OFF state. The LED event duration can be customized by setting the VMD_LED_PERIOD environment variable if a duration other than the default value is desired.
#### Locate an Evicted Device
If an NVMe SSD is evicted, the status LED on the VMD device will be set to a FAULT state, represented by a solidly ON amber light. No additional command apart from the SSD eviction would be needed, and this would visually indicate that the device needs to be replaced and is no longer in use by DAOS. The LED of the VMD device will remain in this state until replaced by a new device.

 Useful admin command to locate a VMD-enabled NVMe SSD:
  - <a href="#85">dmg storage identify vmd</a> [used to change the status LED state on the VMD device to quickly blink for 60 seconds]

<a id="11"></a>
## Device States
The device states that are returned from a device query by the admin are dependent on both the persistently stored device state in SMD, and the in-memory BIO device list.

  - NORMAL: A fully functional device in use by DAOS (or in setup).
  - EVICTED: A device has been manually evicted and is no longer in use by DAOS.
  - UNPLUGGED: A device previously used by DAOS is unplugged.
  - NEW: A new device is available for use by DAOS.

![/doc/graph/dmg_device_states.png](/doc/graph/dmg_device_states.png "Device States")

 Useful admin command to query device states:
   - <a href="#31">dmg storage query list-devices</a> [used to query NVMe SSD device states]

<a id="12"></a>
## User Interfaces:
<a id="80"></a>
- Query Per-Server Metadata (SMD): **$dmg storage query (list-devices | list-pools)**

To list all devices:

```
$ dmg storage query list-devices
Devices
        UUID:9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0 [TrAddr:0000:8d:00.0]
            Targets:[0] Rank:0 State:NORMAL
        UUID:a0e34f6b-06f7-4cb8-aec6-9162e45b8648 [TrAddr:0000:8a:00.0]
            Targets:[1] Rank:0 State:NORMAL
        UUID:0c87e88d-44bf-4b9f-a69d-77b2a26ed4c4 [TrAddr:0000:8b:00.0]
            Targets:[2] Rank:0 State:NORMAL
        UUID:f1623ce1-b383-4927-929f-449fccfbb340 [TrAddr:0000:8c:00.0]
            Targets:[] Rank:0 State:NEW
```
To list all pools:
```
$ dmg storage query list-pools --verbose
Pools
        UUID:8131fc39-4b1c-4662-bea1-734e728c434e
            Rank:0 Targets:[0 2 1] Blobs:[4294967296 4294967296 4294967296]
        UUID:8131fc39-4b1c-4662-bea1-734e728c434e
            Rank:1 Targets:[0 1 2] Blobs:[4294967296 4294967296 4294967296]

```

<a id="81"></a>
- Query Device Health Data: **$dmg storage query (device-health | target-health)**

```
$ dmg storage query device-health --uuid=9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0
or
$ dmg storage query target-health --tgtid=1 --rank=0
Devices:
        UUID:9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0 [TrAddr:0000:8d:00.0]
           Targets:[0] Rank:0 State:NORMAL
           Health Stats:
               Timestamp:Tue Jul 28 20:08:57 UTC 19029
               Temperature:314K(40.85C)
               Controller Busy Time:37m0s
               Power Cycles:96
               Power On Duration:14128h0m0s
               Unsafe Shutdowns:89
               Media errors: 0
               Read errors: 0
               Write errors: 0
               Unmap errors: 0
               Checksum errors: 0
               Error log entries: 0
              Critical Warnings:
               Temperature: OK
               Available Spare: OK
               Device Reliability: OK
               Read Only: OK
               Volatile Memory Backup: OK
```
<a id="82"></a>
- Manually Set Device State to FAULTY: **$dmg storage set nvme-faulty**
```
$ dmg storage set nvme-faulty --uuid=9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0
Devices
        UUID:9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0 [TrAddr:0000:8d:00.0]
            Targets:[0] Rank:0 State:EVICTED

```

<a id="83"></a>
- Replace an evicted device with a new device: **$dmg storage replace nvme**
```
$ dmg storage replace nvme --old-uuid=9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0 --new-uuid=8131fc39-4b1c-4662-bea1-734e728c434e
Devices
        UUID:8131fc39-4b1c-4662-bea1-734e728c434e [TrAddr:0000:8d:00.0]
            Targets:[0] Rank:0 State:NORMAL

```

<a id="84"></a>
- Reuse a previously evicted device: **$dmg storage replace nvme**
```
$ dmg storage replace nvme --old-uuid=9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0 --new-uuid=9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0
Devices
        UUID:9fb3ce57-1841-43e6-8b70-2a5e7fb2a1d0 [TrAddr:0000:8a:00.0]
            Targets:[0] Rank:0 State:NORMAL

```

<a id="#85"></a>
- Identify a VMD-enabled NVMe SSD: **$dmg storage identify vmd**
```
$ dmg storage identify vmd --uuid=57b3ce9f-1841-43e6-8b70-2a5e7fb2a1d0
Devices
       UUID:57b3ce9f-1841-43e6-8b70-2a5e7fb2a1d0 [TrAddr:5d0505:01:00.0]
           Targets:[1] Rank:1 State:IDENTIFY
```


=============================================================================


# Collective and RPC Transport (CaRT)

> :warning: **Warning:** CaRT is under heavy development. Use at your own risk.

CaRT is an open-source RPC transport layer for Big Data and Exascale HPC. It supports both traditional P2P RPC delivering and **collective RPC which invokes the RPC at a group of target servers with a scalable tree-based message propagating** **(collective RPC: 从服务端的group of targets发起的collective RPC)**.

# Gurt Useful Routines and Types (GURT)

GURT is a open-source library of helper functions and data types. The library makes it easy to manipulate lists, hash tables, heaps and logging.

All Gurt Useful Routines and Types are prefixed with 'd', the 4th letter in the alphabet because gurt useful words have 4 letters.

## License

CaRT is open source software distributed under a BSD license.
Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for
more information.

## Build

CaRT requires a C99-capable compiler and the scons build tool to build.

CaRT depends on some third-party libraries:
- Mercury (https://github.com/mercury-hpc/mercury) for RPC and underneath communication
  The mercury uses openpa (http://git.mcs.anl.gov/radix/openpa.git) for atomic operation.
- PMIx (https://github.com/pmix/master)
  The PMIx uses hwloc library (wget https://www.open-mpi.org/software/hwloc/v1.11/downloads/hwloc-1.11.2.tar.gz).
- Openmpi runtime environment
  The ompi needs to be compiled with the external PMIx/hwloc (an example configuration is "./configure --with-pmix=/your_pmix_install_path / --with-hwloc=/your_hwloc_install_path --with-libevent=external").

Can execute "scons" in top source directory to build it when all the dependent modules installed.


==============================================================================


# Versioned Block Allocator

The Versioned Block Allocator is used by VOS for managing blocks on NVMe SSD, it's basically an extent based block allocator specially designed for DAOS.

## Allocation metadata

The blocks allocated by VEA are used to store single value or array value in VOS. Since the address and size from each allocation is tracked in VOS index trees, the VEA allocation metadata tracks only free extents in a btree, more importantly, this allocation metadata is stored on SCM along with the VOS index trees, so that block allocation and index updating could be easily made into single PMDK transaction, at the same time, the performance would be much better than storing the metadata on NVMe SSD.

## Scalable concurrency

Thanks to the shared-nothing architecture of DAOS server, scalable concurrency isn't a design cosideration for VEA, which means VEA doesn't have to split space into zones for mitigating the contention problem.

## Delayed atomicity action

VOS update is executed in a 'delayed atomicity' manner, which consists of three steps:
<ol>
<li>Reserve space for update and track the reservation transiently in DRAM;</li>
<li>Start RMDA transfer to land data from client to reserved space;</li>
<li>Turn the reservation into a persistent allocation and update the allocated address in VOS index within single PMDK transaction;</li>
Obviously, the advantage of this manner is that the atomicity of allocation and index updating can be guaranteed without an undo log to revert the actions in first step.

To support this delayed atomicity action, VEA maintains two sets of allocation metadata, one in DRAM for transient reservation, the other on SCM for persistent allocation.
</ol>

## Allocation hint

VEA assumes a predictable workload pattern: All the block allocate and free calls are from different 'IO streams', and the blocks allocated within the same IO stream are likely to be freed at the same time, so a straightforward conclusion is that external fragmentations could be reduced by making the per IO stream allocations contiguous.

The IO stream model perfectly matches DAOS storage architecture, there are two IO streams per VOS container, one is the regular updates from client or rebuild, the other one is the updates from background VOS aggregation. VEA provides a set of hint API for caller to keep a sequential locality for each IO stream, that requires each caller IO stream to track its own last allocated address and pass it to the VEA as a hint on next allocation.

    
    
    ===============================================================================
    
    
# DAOS Arrays

A DAOS Array is a special DAOS object to expose a logical 1-dimentional array to
the user. The array is created by the user with an immutable record size and
chunk size. Additional APIs are provided to access the array (read, write,
punch).

## Array Representation

The Array representation over the DAOS KV API is done with integer typed DKeys,
where each DKey holds chunk_size records. Each DKey has 1 AKey with a NULL value
that holds the user array data in an array type extent. The first DKey (which is
0) does not hold any user data, but only the array metadata:

~~~~~~
DKey: 0
Single Value: 3 uint64_t
       [0] = magic value (0xdaca55a9daca55a9)
       [1] = array cell size
       [2] = array chunk size
~~~~~~

To illustrate the array mapping, suppose we have a logical array of 10 elements
and chunk size being 3. The DAOS KV representation would be:

~~~~~~
DKey: 1
Array records: 0, 1, 2
DKey: 2
Array records: 3, 4, 5
DKey: 3
Array records: 6, 7, 8
DKey: 4
Array records: 9
~~~~~~

## API and Implementation

The API (include/daos_array.h) includes operations to:
- create an array with the required, immutable metadata of the array.
- open an existing array which returns the metadata associated with the array.
- read from an array object.
- write to an array object.
- set size (truncate) of an array. Note this is not equivaluent to preallocation.
- get size of an array.
- punch a range of records from the array.
- destroy/remove an array.

The Array API is implemented using the DAOS Task API. For example, the read and
write operations create an I/O operation for each DKey and inserts them into the
task engine with a parent task that depends on all the child tasks that do the
I/O.

The API is currently tested with daos_test.

    
    
    ====================================================================================
    
 
 
# Versioning Object Store

The Versioning Object Store (VOS) is responsible for providing and maintaining a persistent object store that supports byte-granular access and versioning for a single shard in a <a href="/doc/storage_model.md#DAOS_Pool">DAOS pool</a>.
It maintains its metadata in persistent memory and may store data either in persistent memory or on block storage, depending on available storage and performance requirements.
It must provide this functionality with minimum overhead so that performance can approach the theoretical performance of the underlying hardware as closely as possible, both with respect to latency and bandwidth.
Its internal data structures, in both persistent and non-persistent memory, must also support the highest levels of concurrency so that throughput scales over the cores of modern processor architectures.
Finally, and critically, it must validate the integrity of all persisted object data to eliminate the possibility of silent data corruption, both in normal operation and under all possible recoverable failures.

This section provides the details for achieving the design goals discussed above in building a versioning object store for DAOS.

This document contains the following sections:

- <a href="#62">Persistent Memory based Storage</a>
    - <a href="#63">In-Memory Storage</a>
    - <a href="#64">Lightweight I/O Stack: PMDK Libraries</a>
- <a href="#71">VOS Concepts</a>
    - <a href="#711">VOS Indexes</a>
    - <a href="#712">Object Listing</a>
-  <a href="#72">Key Value Stores</a>
    - <a href="#721">Operations Supported with Key Value Store</a>
    - <a href="#723">Key in VOS KV Stores</a>
    - <a href="#724">Internal Data Structures</a>
- <a href="#73">Key Array Stores</a>
- <a href="#82">Conditional Update and MVCC</a>
    - <a href="#821">VOS Timestamp Cache</a>
    - <a href="#822">Read Timestamps</a>
    - <a href="#823">Write Timestamps</a>
    - <a href="#824">MVCC Rules</a>
    - <a href="#825">Punch Propagation</a>
- <a href="#74">Epoch Based Operations</a>
    - <a href="#741">VOS Discard</a>
    - <a href="#742">VOS Aggregate</a>
- <a href="#79">VOS Checksum Management</a>
- <a href="#80">Metadata Overhead</a>
- <a href="#81">Replica Consistency</a>
    - <a href="#811">DAOS Two-phase Commit</a>
    - <a href="#812">DTX Leader Election</a>

<a id="62"></a>
## Persistent Memory based Storage

<a id="63"></a>
### In-Memory Storage

The VOS is designed to use a persistent-memory storage model that takes advantage of byte-granular, sub-microsecond storage access possible with new NVRAM technology.
This enables a disruptive change in performance compared to conventional storage systems for application and system metadata and small, fragmented, and misaligned I/O.
Direct access to byte-addressable low-latency storage opens up new horizons where metadata can be scanned in less than a second without bothering with seek time and alignment.

The VOS relies on a log-based architecture using persistent memory primarily to maintain internal persistent metadata indexes.
The actual data can be stored either in persistent memory directly or in block-based NVMe storage.
The DAOS service has two tiers of storage: Storage Class Memory (SCM) for byte-granular application data and metadata, and NVMe for bulk application data.
Similar to how PMDK is currently used to facilitate access to SCM, the Storage Performance Development Kit (<a href="https://spdk.io/">SPDK</a>) is used to provide seamless and efficient access to NVMe SSDs.
The current DAOS storage model involves three DAOS server xstreams per core, along with one main DAOS server xstream per core mapped to an NVMe SSD device.
DAOS storage allocations can occur on either SCM by using a PMDK pmemobj pool, or on NVMe, using an SPDK blob.
All local server metadata will be stored in a per-server pmemobj pool on SCM and will include all current and relevant NVMe devices, pool, and xstream mapping information.
Please refer to the <a href="../bio/README.md">Blob I/O</a> (BIO) module for more information regarding NVMe, SPDK, and per-server metadata.
Special care is taken when developing and modifying the VOS layer because any software bug could corrupt data structures in persistent memory.
The VOS, therefore, checksums its persistent data structures despite the presence of hardware ECC.

The VOS provides a lightweight I/O stack fully in user space, leveraging the <a href="pmem.io">PMDK</a> open-source libraries developed to support this programming model.

<a id="64"></a>

### Lightweight I/O Stack: PMDK Libraries
Although persistent memory is accessible via direct load/store, updates go through multiple levels of caches, including the processor L1/2/3 caches and the NVRAM controller.
Durability is guaranteed only after all those caches have been explicitly flushed.
The VOS maintains internal data structures in persistent memory that must retain some level of consistency so that operation may be resumed without loss of durable data after an unexpected crash or power outage.
The processing of a request will typically result in several memory allocations and updates that must be applied atomically.

Consequently, a transactional interface must be implemented on top of persistent memory to guarantee internal VOS consistency.
It is worth noting that such transactions are different from the DAOS transaction mechanism.
Persistent memory transactions must guarantee consistency of VOS internal data structures when processing incoming requests, regardless of their epoch number.
Transactions over persistent memory can be implemented in many different ways, e.g., undo logs, redo logs, a combination of both, or copy-on-write.

<a href="https://pmem.io">PMDK</a> is an open source collection of libraries for using persistent memory, optimized specifically for NVRAM.
Among these is the libpmemobj library, which implements relocatable persistent heaps called persistent memory pools.
This includes memory allocation, transactions, and general facilities for persistent memory programming.
The transactions are local to one thread (not multi-threaded) and rely on undo logs.
Correct use of the API ensures that all memory operations are rolled back to the last committed state upon opening a pool after a server failure.
VOS utilizes this API to ensure consistency of VOS internal data structures, even in the event of server failures.

<a id="71"></a>
## VOS Concepts

The versioning object store provides object storage local to a storage target by initializing a VOS pool (vpool) as one shard of a DAOS pool.
A vpool can hold objects for multiple object address spaces called containers.
Each vpool is given a unique UID on creation, which is different from the UID of the DAOS pool.
The VOS also maintains and provides a way to extract statistics like total space, available space, and number of objects present in a vpool.

The primary purpose of the VOS is to capture and log object updates in arbitrary time order and integrate these into an ordered epoch history that can be traversed efficiently on demand.
This provides a major scalability improvement for parallel I/O by correctly ordering conflicting updates without requiring them to be serialized in time.
For example, if two application processes agree on how to resolve a conflict on a given update, they may write their updates independently with the assurance that they will be resolved in the correct order at the VOS.

The VOS also allows all object updates associated with a given epoch and process group to be discarded.
This functionality ensures that when a DAOS transaction must be aborted, all associated updates are invisible before the epoch is committed for that process group and becomes immutable.
This ensures that distributed updates are atomic - i.e.
when a commit completes, either all updates have been applied or been discarded.

Finally, the VOS may aggregate the epoch history of objects in order to reclaim space used by inaccessible data and to speed access by simplifying indices.
For example, when an array object is "punched" from 0 to infinity in a given epoch, all data updated after the latest snapshot before this epoch becomes inaccessible once the container is closed.

Internally, the VOS maintains an index of container UUIDs that references each container stored in a particular pool.
The container itself contains three indices.
The first is an object index used to map an object ID and epoch to object metadata efficiently when servicing I/O requests.
The other two indices are for maintining active and committed <a href="#811">DTX</a> records for ensuring efficient updates across multiple replicas.

DAOS supports two types of values, each associated with a Distribution Key (DKEY) and an Attribute Key (AKEY): Single value and Array value.
The DKEY is used for placement, determining which VOS pool is used to store the data.
The AKEY identifies the data to be stored.
The ability to specify both a DKEY and an AKEY provides applications with the flexibility to either distribute or co-locate different values in DAOS.
A single value is an atomic value meaning that writes to an AKEY update the entire value and reads retrieve the latest value in its entirety.
An array value is an index of equally sized records.  Each update to an array value only affects the specified records and reads read the latest updates to each record index requested.
Each VOS pool maintains the VOS provides a per container hierarchy of containers, objects, DKEYs, AKEYs, and values as shown <a href="#7a">below</a>.
The DAOS API provides generic Key-Value and Array abstractions built on this underlying interface.  The former sets the DKEY to the user specified key and uses a fixed AKEY.
The latter uses the upper bits of the array index to create a DKEY and uses a fixed AKEY thus evenly distributing array indices over all VOS pools in the object layout.
For the remainder of the VOS description, Key-Value and Key-Array shall be used to describe the VOS layout rather than these simplifying abstractions.
In other words, they shall describe the DKEY-AKEY-Value in a single VOS pool.

VOS objects are not created explicitly but are created on the first write by creating the object metadata and inserting a reference to it in the owning container's object index.
All object updates log the data for each update, which may be an object, DKEY, AKEY, a single value, or array value punch or an update to a single value or array value.
Note that "punch" of an extent of an array object is logged as zeroed extents, rather than causing relevant array extents or key values to be discarded. A punch of an object, DKEY, AKEY, or single value is logged, so that reads at a later timestamp see no data.
This ensures that the full version history of objects remain accessible.   The DAOS api, however, only allows accessing data at snapshots so VOS aggregation can aggressively remove objects, keys, and values that are no longer accessible at a known snapshot.

<a id="7a"></a>
![../../doc/graph/Fig_067.png](../../doc/graph/Fig_067.png "VOS Pool storage layout")

When performing lookup on a single value in an object, the object index is traversed to find the index node with the highest epoch number less than or equal to the requested epoch (near-epoch) that matches the key.
If a value or negative entry is found, it is returned.
Otherwise, a "miss" is returned, meaning that this key has never been updated in this VOS.
This ensures that the most recent value in the epoch history of is returned irrespective of the time-order in which they were integrated and that all updates after the requested epoch are ignored.

Similarly, when reading an array object, its index is traversed to create a gather descriptor that collects all object extent fragments in the requested extent with the highest epoch number less than or equal to the requested epoch.
Entries in the gather descriptor either reference an extent containing data, a punched extent that the requestor can interpret as all zeroes, or a "miss", meaning that this VOS has received no updates in this extent.
Again, this ensures that the most recent data in the epoch history of the array is returned for all offsets in the requested extent, irrespective of the time-order in which they were written, and that all updates after the requested epoch are ignored.

<a id="711"></a>

### VOS Indexes

The value of the object index table, indexed by OID, points to a DKEY index.
The values in the DKEY index, indexed by DKEY, point to an AKEY index.
The values in the AKEY index, indexed by AKEY, point to either a Single Value index or an Array index.
A single value index is referenced by epoch and will return the latest value inserted at or prior to the epoch.
An array value is indexed by the extent and the epoch and will return portions of extents visible at the epoch.

Hints about the expectations of the object can be encoded in the object ID.
For example, an object can be replicated, erasure coded, use checksums, or have integer or lexical DKEYs and/or AKEYs.
If integer or lexical keys are used, the object index is ordered by keys, making queries, such as array size, more efficient.
Otherwise, keys are ordered by the hashed value in the index.
The object ID is 128 bits.  The upper 32 bits are used to encodes the object type, and key types while the lower 96 bits are a user defined identifier that must be unique to the container.

Each object, dkey, and akey has an associated incarnation log.  The incarnation
log can be described as an in-order log of creation and punch events for the
associated entity.   The log is checked for each entity in the path to the value
to ensure the entity, and therefore the value, is visible at the requested
time.

<a id="712"></a>

### Object Listing

VOS provides a generic iterator that can be used to iterate through containers, objects, DKEYs, AKEYs, single values, and array extents in a VOS pool.
The iteration API is shown in the <a href="#7b">figure</a> below.

<a id="7b"></a>
```C
/**
 * Iterate VOS entries (i.e., containers, objects, dkeys, etc.) and call \a
 * cb(\a arg) for each entry.
 *
 * If \a cb returns a nonzero (either > 0 or < 0) value that is not
 * -DER_NONEXIST, this function stops the iteration and returns that nonzero
 * value from \a cb. If \a cb returns -DER_NONEXIST, this function completes
 * the iteration and returns 0. If \a cb returns 0, the iteration continues.
 *
 * \param[in]		param		iteration parameters
 * \param[in]		type		entry type of starting level
 * \param[in]		recursive	iterate in lower level recursively
 * \param[in]		anchors		array of anchors, one for each
 *					iteration level
 * \param[in]		cb		iteration callback
 * \param[in]		arg		callback argument
 *
 * \retval		0	iteration complete
 * \retval		> 0	callback return value
 * \retval		-DER_*	error (but never -DER_NONEXIST)
 */
int
vos_iterate(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
	    struct vos_iter_anchors *anchors, vos_iter_cb_t cb, void *arg);
```

The generic VOS iterator API enables both the DAOS enumeration API as well as DAOS internal features supporting rebuild, aggregation, and discard.
It is flexible enough to iterate through all keys, single values, and extents for a specified epoch range.
Additionally, it supports iteration through visible extents.

<a id="72"></a>

## Key Value Stores (Single Value)

High-performance simulations generating large quantities of data require indexing and analysis of data, to achieve good insight.
Key Value (KV) stores can play a vital role in simplifying the storage of such complex data and allowing efficient processing.

VOS provides a multi-version, concurrent KV store on persistent memory that can grow dynamically and provide quick near-epoch retrieval and enumeration of key values.

Although there is an array of previous work on KV stores, most of them focus on cloud environments and do not provide effective versioning support.
Some KV stores provide versioning support but expect monotonically increasing ordering of versions and further, do not have the concept of near-epoch retrieval.

VOS must be able to accept insertion of KV pairs at any epoch and must be able to provide good scalability for concurrent updates and lookups on any key-value object.
KV objects must also be able to support any type and size of keys and values.


<a id="721"></a>

### Operations Supported with Key Value Store

VOS supports large keys and values with four types of operations; update, lookup, punch, and key enumeration.

The update and punch operations add a new key to a KV store or log a new value of an existing key.
Punch logs the special value "punched", effectively a negative entry, to record the epoch when the key was deleted.
Sharing the same epoch for both an update and a punch of the same object, key, value, or extent is disallowed, and VOS will return an error when such is attempted.

Lookup traverses the KV metadata to determine the state of the given key at the given epoch.
If the key is not found at all, a "miss" is returned to indicate that the key is absent from this VOS.
Otherwise, the value at the near-epoch or greatest epoch less than or equal to the requested epoch is returned.
If this is the special "punched" value, it means the key was deleted in the requested epoch.
The value here refers to the value in the internal tree-data structure.
The key-value record of the KV-object is stored in the tree as the value of its node.
So in case of punch this value contains a "special" return code/flag to identify the punch operation.

VOS also supports the enumeration of keys belonging to a particular epoch.

<a id="723"></a>

### Key in VOS KV Stores

VOS KV supports key sizes from small keys to extremely large keys.
For AKEYs and DKEYs, VOS supports either hashed keys or one of two types of
"direct" keys: lexical or integer.
#### Hashed Keys
The most flexible key type is the hashed key.   VOS runs two fast hash
algorithms on the user supplied key and uses the combined hashed key values for
the index.  The intention of the combined hash is to avoid collisions between
keys. The actual key still must be compared for correctness.
#### Direct Keys
The use of hashed keys results in unordered keys.  This is problematic in cases
where the user's algorithms may benefit from ordering.   Therefore, VOS supports
two types of keys that are not hashed but rather interpreted directly.
##### Lexical Keys
Lexical keys are compared using a lexical ordering.  This enables usage such as
sorted strings.   Presently, lexical keys are limited in length, however to
80 characters.
##### Integer Keys
Integer keys are unsigned 64-bit integers and are compared as such.   This
enables use cases such as DAOS array API using the upper bits of the index
as a dkey and the lower bits as an offset. This enables such objects to use the
the DAOS key query API to calculate the size more efficiently.

KV stores in VOS allow the user to maintain versions of the different KV pairs in random order.
For example, an update can happen in epoch 10, and followed by another update in epoch 5, where HCE is less than 5.
To provide this level of flexibility, each key in the KV store must maintain the epoch of update/punch along with the key.
The ordering of entries in index trees first happens based on the key, and then based on the epochs.
This kind of ordering allows epochs of the same key to land in the same subtree, thereby minimizing search costs.
Conflict resolution and tracking is performed using <a href="#81">DTX</a> described later.
DTX ensures that replicas are consistent, and failed or uncommitted updates are not visible externally.

<a id="724"></a>

### Internal Data Structures

Designing a VOS KV store requires a tree data structure that can grow dynamically and remain self-balanced.
The tree needs to be balanced to ensure that time complexity does not increase with an increase in tree size.
Tree data structures considered are red-black trees and B+ Trees, the former is a binary search tree, and the latter an n-ary search tree.

Although red-black trees provide less rigid balancing compared to AVL trees, they compensate by having cheaper rebalancing cost.
Red-black trees are more widely used in examples such as the Linux kernel, the java-util library, and the C++ standard template library.
B+ trees differ from B trees in the fact they do not have data associated with their internal nodes.
This can facilitate fitting more keys on a page of memory.
In addition, leaf-nodes of B+ trees are linked; this means doing a full scan would require just one linear pass through all the leaf nodes, which can potentially minimize cache misses to access data in comparison to a B Tree.

To support update and punch as mentioned in the previous section (<a href="#721">Operations Supported with Key Value Stores</a>), an epoch-validity range is set along with the associated key for every update or punch request, which marks the key to be valid from the current epoch until the highest possible epoch.
Updates to the same key on a future epoch or past epoch modify the end epoch validity of the previous update or punch accordingly.
This way only one key has a validity range for any given key-epoch pair lookup while the entire history of updates to the key is recorded.
This facilitates nearest-epoch search.
Both punch and update have similar keys, except for a simple flag identifying the operation on the queried epoch.
Lookups must be able to search a given key in a given epoch and return the associated value.
In addition to the epoch-validity range, the container handle cookie generated by DAOS is also stored along with the key of the tree.
This cookie is required to identify behavior in case of overwrites on the same epoch.

A simple example input for crearting a KV store is listed in the <a href="#7c">Table</a> below.
Both a B+ Tree based index and a red-black tree based index are shown in the <a href="#7c">Table</a> and <a href="#7d"> figure</a> below, respectively.
For explanation purposes, representative keys and values are used in the example.

<a id="7c"></a>
<b>Example VOS KV Store input for Update/Punch</b>

|Key|Value|Epoch|Update (U/P)|
|---|---|---|---|
|Key 1|Value 1|1|U|
|Key 2|Value 2|2|U|
|Key 3|Value 3|4|U|
|Key 4|Value 4|1|U|
|Key 1|NIL|2|P|
|Key 2|Value 5|4|U|
|Key 3|Value 6|1|U|

<a id="7d"></a>

![../../doc/graph/Fig_011.png](../../doc/graph/Fig_011.png "Red Black Tree based KV Store with Multi-Key")

The red-black tree, like any traditional binary tree, organizes the keys lesser than the root to the left subtree and keys greater than the root to the right subtree.
Value pointers are stored along with the keys in each node.
On the other hand, a B+ Tree-based index stores keys in ascending order at the leaves, which is where the value is stored.
The root nodes and internal nodes (color-coded in blue and maroon accordingly) facilitate locating the appropriate leaf node.
Each B+ Tree node has multiple slots, where the number of slots is determined from the order.
The nodes can have a maximum of order-1 slots.
The container handle cookie must be stored with every key in case of red-black trees, but in case of B+ Trees having cookies only in leaf nodes would suffice, since cookies are not used in traversing.

In the <a href="#7e">table</a> below, n is the number of entries in the tree, m is the number of keys, k is the number of the key, epoch entries between two unique keys.

<b>Comparison of average case computational complexity for index</b>
<a id="7e"></a>

|Operation|Red-black tree|B+Tree|
|---|---|---|
|Update|O(log2n)|O(log<sub>b</sub>n)|
|Lookup|O(log2n)|O(log<sub>b</sub>n)|
|Delete|O(log2n)|O(log<sub>b</sub>n)|
|Enumeration|O(m* log<sub>2</sub>(n) + log<sub>2</sub>(n))|O(m * k + log<sub>b</sub> (n))|

Although both these solutions are viable implementations, determining the ideal data structure would depend on the performance of these data structures on persistent memory hardware.

VOS also supports concurrent access to these structures, which mandates that the data structure of choice provides good scalability while there are concurrent updates.
Compared to B+ Tree, rebalancing in red-black trees causes more intrusive tree structure change; accordingly, B+ Trees may provide better performance with concurrent accesses.
Furthermore, because B+ Tree nodes contain many slots depending on the size of each node, prefetching in cache can potentially be easier.
In addition, the sequential computational complexities in the <a href="#7e">Table</a> above show that a B+ Tree-based KV store with a reasonable order, can perform better in comparison to a Red-black tree.

VOS supports enumerating keys valid in a given epoch.
VOS provides an iterator-based approach to extract all the keys and values from a KV object.
Primarily, KV indexes are ordered by keys and then by epochs.
With each key holding a long history of updates, the size of a tree can be huge.
Enumeration with a tree-successors approach can result in an asymptotic complexity of O(m* log (n) + log (n)) with red-black trees, where m is the number of keys valid in the requested epoch.
It takes O(log2 (n)) to locate the first element in the tree and O(log2 (n)) to locate a successor.
Because "m" keys need to be retrieved, O( m * log2 (n)) would be the complexity of this enumeration.

In the case of B+-trees, leaf nodes are in ascending order, and enumeration would be to parse the leaf nodes directly.
The complexity would be O (m * k + logbn), where m is the number of keys valid in an epoch, k is the number of entries between two different keys in B+ tree leaf nodes, and b is the order for the B+tree.
Having "k" epoch entries between two distinct keys incurs in a complexity of O(m * k).
The additional O(logbn) is required to locate the first leftmost key in the tree.
The generic iterator interfaces as shown in <a href="#7d">Figure</a> above would be used for KV enumeration also.

In addition to the enumeration of keys for an object valid in an epoch, VOS also supports enumerating keys of an object modified between two epochs.
The epoch index table provides keys updated in each epoch.
On aggregating the list of keys associated with each epoch, (by keeping the latest update of the key and discarding the older versions) VOS can generate a list of keys with their latest epoch.
By looking up each key from the list in its associated index data structure, VOS can extract values with an iterator-based approach.

<a id="73"></a>

## Key Array Stores

The second type of object supported by VOS is a Key-Array object.
Array objects, similar to KV stores, allow multiple versions and must be able to write, read, and punch any part of the byte extent range concurrently.
The <a href="#7f">figure</a> below shows a simple example of the extents and epoch arrangement within a Key-Array object.
In this example, the different lines represent the actual data stored in the respective extents and the color-coding points to different threads writing that extent range.

<a id="7f"></a>

<b>Example of extents and epochs in a Key Array object</b>

![../../doc/graph/Fig_012.png](../../doc/graph/Fig_012.png "Example of extents and epochs in a byte array object")

In the <a href="7f">above</a> example, there is significant overlap between different extent ranges.
VOS supports nearest-epoch access, which necessitates reading the latest value for any given extent range.
For example, in the <a href="#7f">figure</a> above, if there is a read request for extent range 4 - 10 at epoch 10, the resulting read buffer should contain extent 7-10 from epoch 9, extent 5-7 from epoch 8, and extent 4-5 from epoch 1.
VOS array objects also support punch over both partial and complete extent ranges.

<a id="7g"></a>

<b>Example Input for Extent Epoch Table</b>

|Extent Range|Epoch |Write (or) Punch|
|---|---|---|
|0 - 100|1|Write|
|300 - 400|2|Write
|400 - 500|3|Write|
|30 - 60|10|Punch|
|500 - 600|8|Write|
|600 - 700|9|Write|

<a id="7j"></a>

R-Trees provide a reasonable way to represent both extent and epoch validity ranges in such a way as to limit the search space required to handle a read request.
VOS provides a specialized R-Tree, called an Extent-Validity tree (EV-Tree) to store and query versioned array indices.
In a traditional R-Tree implementation, rectangles are bounded and immutable.
In VOS, the "rectangle" consists of the extent range on one axis and the epoch validity range on the other.
However, the epoch validity range is unknown at the time of insert so all rectangles are inserted assuming an upper bound of infinity.
Originally, the DAOS design called for splitting such in-tree rectangles on insert to bound the validity range but a few factors resulted in the decision to keep the original validity range.  First, updates to persistent memory are an order of magnitude more expensive than lookups.  Second, overwrites between snapshots can be deleted by aggregation, thus maintaining a reasonably small history of overlapping writes.   As such, the EV-Tree implements a two part algorithm on fetch.
1. Find all overlapping extents.  This will include all writes that happened before the requested epoch, even if they are covered by a subsequent write.
2. Sort this by extent start and then by epoch
3. Walk through the sorted array, splitting extents if necessary and marking them as visible as applicable
4. Re-sort the array.  This final sort can optionally keep or discard holes and covered extents, depending on the use case.

TODO: Create a new figure
<a id="7k"></a>
<b>Rectangles representing extent_range.epoch_validity arranged in 2-D space for an order-4 EV-Tree using input in the table <a href="#7g">above</a></b>

![../../doc/graph/Fig_016.png](../../doc/graph/Fig_016.png "Rectangles representing extent_range.epoch_validity arranged in 2-D space for an order-4 EV-Tree using input in the table")

The figure <a href="7l">below</a> shows the rectangles constructed with splitting and trimming operations of EV-Tree for the example in the previous <a href="#7g">table</a> with an additional write at offset {0 - 100} introduced to consider the case for extensive splitting.
The figure <a href="#7k">above</a> shows the EV-Tree construction for the same example.

<a id="7l"></a>

<b>Tree (order - 4) for the example in Table 6 3 (pictorial representation shown in the figure <a href="#7g">above</a></b>

![../../doc/graph/Fig_017.png](../../doc/graph/Fig_017.png "Rectangles representing extent_range.epoch_validity arranged in 2-D space for an order-4 EV-Tree using input in the table")

Inserts in an EV-Tree locate the appropriate leaf-node to insert, by checking for overlap.
If multiple bounding boxes overlap, the bounding box with the least enlargement is chosen.
Further ties are resolved by choosing the bounding box with the least area.
The maximum cost of each insert can be O (log<sub>b</sub>n).

Searching an EV-Tree would work similar to R-Tree, aside from the false overlap issue described above.
All overlapping internal nodes must be pursued, till there are matching internal nodes and leaves.
Since extent ranges can span across multiple rectangles, a single search can hit multiple rectangles.
In an ideal case (where the entire extent range falls on one rectangle), the read cost is O(log<sub>b</sub>n) where b is the order of the tree.
The sorting and splitting phase adds the additional overhead of O(n log n) where n is the number of matching extents.
In the worst case, this is equivalent to all extents in the tree, but this is mitigated by aggregation and the expectation that the tree associated with a single shard of a single key will be relatively small.

For deleting nodes from an EV-Tree, the same approach as search can be used to locate nodes, and nodes/slots can be deleted.
Once deleted, to coalesce multiple leaf-nodes that have less than order/2 entries, reinsertion is done.
EV-tree reinserts are done (instead of merging leaf-nodes as in B+ trees) because on deletion of leaf node/slots, the size of bounding boxes changes, and it is important to make sure the rectangles are organized into minimum bounding boxes without unnecessary overlaps.
In VOS, delete is required only during aggregation and discard operations.
These operations are discussed in a following section (<a href="#74">Epoch Based Operations</a>).

<a id="82"></a>
## Conditional Update and MVCC

VOS supports conditional operations on individual dkeys and akeys.  The
following operations are supported:

- Conditional fetch:  Fetch if the key exists, fail with -DER_NONEXIST otherwise
- Conditional update: Update if the key exists, fail with -DER_NONEXIST otherwise
- Conditional insert: Update if the key doesn't exist, fail with -DER_EXIST otherwise
- Conditional punch:  Punch if the key exists, fail with -DER_NONEXIST otherwise

These operations provide atomic operations enabling certain use cases that
require such.  Conditional operations are implemented using a combination of
existence checks and read timestamps.   The read timestamps enable limited
MVCC to prevent read/write races and provide serializability guarantees.

<a id="821"><a>
### VOS Timestamp Cache

VOS maintains an in-memory cache of read and write timestamps in order to
enforce MVCC semantics.  The timestamp cache itself consists of two parts:

1. Negative entry cache. A global array per target for each type of entity
including objects, dkeys, and akeys.  The index at each level is determined by
the combination of the index of the parent entity, or 0 in the case of
containers, and the hash of the entity in question.   If two different keys map
to the same index, they share timestamp entries.   This will result in some
false conflicts but does not affect correctness so long as progress can be made.
The purpose of this array is to store timestamps for entries that do not exist
in the VOS tree.   Once an entry is created, it will use the mechanism described
in #2 below.  Note that multiple pools in the same target use this shared
cache so it is also possible for false conflicts across pools before an
entity exists.  These entries are initialized at startup using the global
time of the starting server.   This ensures that any updates at an earlier
time are forced to restart to ensure we maintain automicity since timestamp
data is lost when a server goes down.
2. Positive entry cache. An LRU cache per target for existing containers,
objects, dkeys, and akeys.  One LRU array is used for each level such that
containers, objects, dkeys, and akeys only conflict with cache entries of the
same type.  Some accuracy is lost when existing items are evicted from the cache
as the values will be merged with the corresponding negative entry described in
#1 above until such time as the entry is brought back into cache.   The index of
the cached entry is stored in the VOS tree though it is only valid at runtime.
On server restarts, the LRU cache is initialized from the global time when the
restart occurs and all entries are automatically invalidated.  When a new entry
is brought into the LRU, it is initialized using the corresponding negative
entry.  The index of the LRU entry is stored in the VOS tree providing O(1)
lookup on subsequent accesses.

<a id="822"></a>
### Read Timestamps

Each entry in the timestamp cache contains two read timestamps in order to
provide serializability guarantees for DAOS operations.  These timestamps are

1. A low timestamp (entity.low) indicating that _all_ nodes in the subtree
rooted at the entity have been read at entity.low
2. A high timestamp (entity.high) indicating that _at least_ one node in the
subtree rooted at the entity has been read at entity.high.

For any leaf node (i.e., akey), low == high; for any non-leaf node, low <= high.

The usage of these timestamps is described <a href="#824">below</a>

<a id="823"></a>
### Write Timestamps

In order to detect epoch uncertainty violations, VOS also maintains a pair of
write timestamps for each container, object, dkey, and akey. Logically,
the timestamps represent the latest two updates to either the entity itself
or to an entity in a subtree. At least two timestamps are required to avoid
assuming uncertainty if there are any later updates.  The figure
<a href="#8a">below</a> shows the need for at least two timestamps.  With a
single timestamp only, the first, second, and third cases would be
indistinguishable and would be rejected as uncertain.  The most accurate write
timestamp is used in all cases.  For instance, if the access is an array fetch,
we will check for conflicting extents in the absence of an uncertain punch of
the corresponding key or object.

<a id="8a"></a>
<b>Scenarios illustrating utility of write timestamp cache</b>

![../../doc/graph/uncertainty.png](../../doc/graph/uncertainty.png "Scenarios illustrating utility of write timestamp cache")

<a id="824"></a>
### MVCC Rules

Every DAOS I/O operation belongs to a transaction. If a user does not associate
an operation with a transaction, DAOS regards this operation as a
single-operation transaction. A conditional update, as defined above, is
therefore regarded as a transaction comprising a conditional check, and if the
check passes, an update, or punch operation.

Every transaction gets an epoch. Single-operation transactions and conditional
updates get their epochs from the redundancy group servers they access,
snapshot read transactions get their epoch from the snapshot records and every
other transaction gets its epoch from the HLC of the first server it accesses.
(Earlier implementations use client HLCs to choose epochs in the last case. To
relax the clock synchronization requirement for clients, later implementations
have moved to use server HLCs to choose epochs, while introducing client HLC
Trackers that track the highest server HLC timestamps clients have heard of.) A
transaction performs all operations using its epoch.

The MVCC rules ensure that transactions execute as if they are serialized in
their epoch order while ensuring that every transaction observes all
conflicting transactions commit before it opens, as long as the
system clock offsets are always within the expected maximum system clock offset
(epsilon). For convenience, the rules classify the I/O operations into reads
and writes:

  - Reads
      - Fetch akeys [akey level]
      - Check object emptiness [object level]
      - Check dkey emptiness [dkey level]
      - Check akey emptiness [akey level]
      - List objects under container [container level]
      - List dkeys under object [object level]
      - List akeys under dkey [dkey level]
      - List recx under akey [akey level]
      - Query min/max dkeys under object [object level]
      - Query min/max akeys under dkey [dkey level]
      - Query min/max recx under akey [akey level]
  - Writes
      - Update akeys [akey level]
      - Punch akeys [akey level]
      - Punch dkey [dkey level]
      - Punch object [object level]

And each read or write is at one of the four levels: container, object, dkey,
and akey. An operation is regarded as an access to the whole subtree rooted at
its level. Although this introduces a few false conflicts (e.g., a list
operation versus a lower level update that does not change the list result),
the assumption simplifies the rules.

A read at epoch e follows these rules:

    // Epoch uncertainty check
    if e is uncertain
        if there is any overlapping, unaborted write in (e, e_orig + epsilon]
            reject

    find the highest overlapping, unaborted write in [0, e]
    if the write is not committed
        wait for the write to commit or abort
        if aborted
            retry the find skipping this write

    // Read timestamp update
    for level i from container to the read's level lv
        update i.high
    update lv.low

A write at epoch e follows these rules:

    // Epoch uncertainty check
    if e is uncertain
        if there is any overlapping, unaborted write in (e, e_orig + epsilon]
            reject

    // Read timestamp check
    for level i from container to one level above the write
        if (i.low > e) || ((i.low == e) && (other reader @ i.low))
            reject
    if (i.high > e) || ((i.high == e) && (other reader @ i.high))
        reject

    find if there is any overlapping write at e
    if found and from a different transaction
        reject

A transaction involving both reads and writes must follow both sets of rules.
As optimizations, single-read transactions and snapshot (read) transactions
do not need to update read timestamps. Snapshot creations, however, must
update the read timestamps as if it is a transaction reading the whole
container.

When a transaction is rejected, it restarts with the same transaction ID but a
higher epoch. If the epoch becomes higher than the original epoch plus epsilon,
the epoch becomes certain, guaranteeing the restarts due to the epoch
uncertainty checks are bounded.

Deadlocks among transactions are impossible. A transaction t_1 with epoch e_1
may block a transaction t_2 with epoch e_2 only when t_2 needs to wait for
t_1's writes to commit. Since the client caching is used, t_1 must be
committing, whereas t_2 may be reading or committing. If t_2 is reading, then
e_1 <= e_2. If t_2 is committing, then e_1 < e_2. Suppose there is a cycle of
transactions reaching a deadlock. If the cycle includes a committing-committing
edge, then the epochs along the cycle must increase and then decrease, causing
a contradiction. If all edges are committing-reading, then there must be two
such edges together, causing a contradiction that a reading transaction cannot
block other transactions. Deadlocks are, therefore, not a concern.

If an entity keeps getting reads with increasing epochs, writes to this entity
may keep being rejected due to the entity's ever-increasing read timestamps.
Exponential backoffs with randomizations (see d_backoff_seq) have been
introduced during daos_tx_restart calls. These are effective for dfs_move
workloads, where readers also write.

<a id="825"></a>
### Punch propagation

Since conditional operations rely on an emptiness semantic, VOS read
operations, particularly listing can be very expensive because they would
require potentially reading the subtree to see if the entity is empty or not.
In order to alieviate this problem, VOS instead does punch propagation.
On a punch operation, the parent tree is read to see if the punch
causes it to be empty.  If it does, the parent tree is punched as well.
Propagation presently stops at the dkey level, meaning the object will
not be punched. Punch propagation only applies when punching keys, not
values.

<a id="74"></a>

## Epoch Based Operations

Epochs provide a way for modifying VOS objects without destroying the history of updates/writes.
Each update consumes memory and discarding unused history can help reclaim unused space.
VOS provides methods to compact the history of writes/updates and reclaim space in every storage node.
VOS also supports rollback of history in case transactions are aborted.
The DAOS API timestamp corresponds to a VOS epoch.
The API only allows reading either the latest state or from a persistent snapshot, which is simply a reference on a given epoch.

To compact epochs, VOS allows all epochs between snapshots to be aggregated, i.e., the value/extent-data of the latest epoch of any key is always kept over older epochs.
This also ensures that merging history does not cause loss of exclusive updates/writes made to an epoch.
To rollback history, VOS provides the discard operation.

```C
int vos_aggregate(daos_handle_t coh, daos_epoch_range_t *epr);
int vos_discard(daos_handle_t coh, daos_epoch_range_t *epr);
int vos_epoch_flush(daos_handle_t coh, daos_epoch_t epoch);
```

Aggregate and discard operations in VOS accept a range of epochs to be aggregated normally corresponding to ranges between persistent snapshots.

<a id="741"></a>

### VOS Discard

Discard forcefully removes epochs without aggregation.
This operation is necessary only when the value/extent-data associated with a pair needs to be discarded.
During this operation, VOS looks up all objects associated with each cookie in the requested epoch range from the cookie index table and removes the records directly from the respective object trees by looking at their respective epoch validity.
DAOS requires a discard to service abort requests.
Abort operations require a discard to be synchronous.

During discard, keys and byte-array rectangles need to be searched for nodes/slots whose end-epoch is (discard_epoch -  1).
This means that there was an update before the now discarded epoch, and its validity got modified to support near-epoch lookup.
This epoch validity of the previous update has to be extended to infinity to ensure future lookups at near-epoch would fetch the last known updated value for the key/extent range.

<a id="742"></a>

### VOS Aggregate

During aggregation, VOS must retain the latest update to a key/extent-range discarding the others and any updates visible at a persistent snapshot.
VOS can freely remove or consolidate keys or extents so long as it doesn't alter the view visible at the latest timestamp or any persistent snapshot epoch.
Aggregation makes use of the vos_iterate API to find both visible and hidden entries between persistent snapshots and removes hidden keys and extents and merges contiguous partial extents to reduce metadata overhead.
Aggregation can be an expensive operation but doesn't need to consume cycles on the critical path.
A special aggregation ULT processes aggregation, frequently yielding to avoid blocking the continuing I/O.

<a id="79"></a>

## VOS Checksum Management

VOS is responsible for storing checksums during an object update and retrieve checksums on an object fetch.
Checksums will be stored with other VOS metadata in storage class memory.  For Single Value types, a single checksum is stored.
For Array Value types, multiple checksums can be stored based on the chunk size.

The **Chunk Size** is defined as the maximum number of bytes of data that a checksum is derived from.
While extents are defined in terms of records, the chunk size is defined in terms of bytes.
When calculating the number of checksums needed for an extent, the number of records and the record size is needed.
Checksums should typically be derived from Chunk Size bytes, however,
if the extent is smaller than Chunk Size or an extent is not "Chunk Aligned," then a checksum might be derived from bytes smaller than Chunk Size.

The **Chunk Alignment** will have an absolute offset, not an I/O offset. So even if an extent is exactly, or less than, Chunk Size bytes long, it may have more than one Chunk if it crosses the alignment barrier.

### Configuration
Checksums will be configured for a container when a container is created. Checksum specific properties can be included in the daos_cont_create API.
This configuration has not been fully implemented yet, but properties might include checksum type, chunk size, and server side verification.

### Storage
Checksums will be stored in a record(vos_irec_df) or extent(evt_desc) structure for Single Value types and Array Value types respectfully.
Because the checksum can be of variable size, depending on the type of checksum configured, the checksum itself will be appended to the end of the structure.
The size needed for checksums is included while allocating memory for the persistent structures on SCM (vos_reserve_single/vos_reserve_recx).

The following diagram illustrates the overall VOS layout and where checksums will be stored. Note that the checksum type isn't actually stored in vos_cont_df yet.

![../../doc/graph/Fig_021.png](../../doc/graph/Fig_021.png "How checksum fits into the VOS Layout")


### Checksum VOS Flow (vos_obj_update/vos_obj_fetch)

On update, the checksum(s) are part of the I/O Descriptor. Then, in
akey_update_single/akey_update_recx, the checksum buffer pointer is included in
the internal structures used for tree updates (vos_rec_bundle for SV and
evt_entry_in for EV). As already mentioned, the size of the persistent structure
allocated includes the size of the checksum(s). Finally, while storing the
record (svt_rec_store) or extent (evt_insert), the checksum(s) are copied to the
end of the persistent structure.

On a fetch, the update flow is essentially reversed.

For reference, key junction points in the flows are:

 - SV Update: 	vos_update_end 	-> akey_update_single 	-> svt_rec_store
 - Sv Fetch: 	vos_fetch_begin -> akey_fetch_single 	-> svt_rec_load
 - EV Update: 	vos_update_end 	-> akey_update_recx 	-> evt_insert
 - EV Fetch: 	vos_fetch_begin -> akey_fetch_recx 	-> evt_fill_entry

<a id="80"></a>

## Metadata Overhead

There is a tool available to estimate the metadata overhead. It is described on the <a href="https://github.com/daos-stack/daos/blob/master/src/client/storage_estimator/README.md">storage estimator</a> section.

<a id="81"></a>

## Replica Consistency

DAOS supports multiple replicas for data high availability.  Inconsistency
between replicas is possible when a target fails during an update to a
replicated object and when concurrent updates are applied on replicated targets
in an inconsistent order.

The most intuitive solution to the inconsistency problem is distributed lock
(DLM), used by some distributed systems, such as Lustre.  For DAOS, a user-space
system with powerful, next generation hardware, maintaining distributed locks
among multiple, independent application spaces will introduce unacceptable
overhead and complexity.  DAOS instead uses an optimized two-phase commit
transaction to guarantee consistency among replicas.

<a id="811"></a>
### Single redundancy group based DAOS Two-Phase Commit (DTX)

When an application wants to modify (update or punch) a multiple replicated
object or EC object, the client sends the modification RPC to the leader shard
(via <a href="#812">DTX Leader Election</a> algorithm discussed below). The
leader dispatches the RPC to the other related shards, and each shard makes
its modification in parallel.  Bulk transfers are not forwarded by the leader
but rather transferred directly from the client, improving load balance and
decreasing latency by utilizing the full client-server bandwidth.

Before modifications are made, a local transaction, called 'DTX', is started
on each related shard (both leader and non-leaders) with a client generated
DTX identifier that is unique for the modification within the container. All
the modifications in a DTX are logged in the DTX transaction table and back
references to the table are kept in related modified record.  After local
modifications are done, each non-leader marks the DTX state as 'prepared' and
replies to the leader. The leader sets the DTX state to 'committable' as soon
as it has completed its modifications and has received successful replies from
all non-leaders.  If any shard(s) fail to execute the modification, it will
reply to the leader with failure, and the leader will globally abort the DTX.
Once the DTX is set by the leader to 'committable' or 'aborted', it replies to
the client with the appropriate status.

The client may consider a modification complete as soon as it receives a
successful reply from the leader, regardless of whether the DTX is actually
'committed' or not. It is the responsibility of the leader to commit the
'committable' DTX asynchronously. This can happen if the 'committable' count
or DTX age exceed some thresholds or the DTX is piggybacked via other
dispatched RPCs due to potential conflict with subsequent modifications.

When an application wants to read something from an object with multiple
replicas, the client can send the RPC to any replica.  On the server side,
if the related DTX has been committed or is committable, the record can be
returned to. If the DTX state is prepared, and the replica is not the leader,
it will reply to the client telling it to send the RPC to the leader instead.
If it is the leader and is in the state 'committed' or 'committable', then
such entry is visible to the application. Otherwise, if the DTX on the leader
is also 'prepared', then for transactional read, ask the client to wait and
retry via returning -DER_INPROGRESS; for non-transactional read, related entry
is ignored and the latest committed modification is returned to the client.

If the read operation refers to an EC object and the data read from a data
shard (non-leader) has a 'prepared' DTX, the data may be 'committable' on the
leader due to the aforementioned asynchronous batched commit mechanism.
In such case, the non-leader will refresh related DTX status with the leader.
If the DTX status after refresh is 'committed', then related data can be
returned to the client; otherwise, if the DTX state is still 'prepared', then
for transactional read, ask the client to wait and retry via returning
-DER_INPROGRESS; for non-transactional read, related entry is ignored and the
latest committed modification is returned to the client.

The DTX model is built inside a DAOS container. Each container maintains its own
DTX table that is organized as two B+trees in SCM: one for active DTXs and the
other for committed DTXs.
The following diagram represents the modification of a replicated object under
the DTX model.

<b>Modify multiple replicated object under DTX model</b>

![../../doc/graph/Fig_066.png](../../doc/graph/Fig_066.png ">Modify multiple replicated object under DTX model")

<a id="812"></a>

### Single redundancy group based DTX Leader Election

In single redundancy group based DTX model, the leader selection is done for
each object or dkey following these general guidelines:

R1: When different replicated objects share the same redundancy group, the same
leader should not be used for each object.

R2: When a replicated object with multiple DKEYs span multiple redundancy
groups, the leaders in different redundancy groups should be on different
servers.

R3: Servers that fail frequently should be avoided in leader selection to avoid
frequent leader migration.

R4: For EC object, the leader will be one of the parity nodes within current
redundancy group.

    
    =========================================================================
    
    
# DAOS Data Plane (aka daos_engine)

## Module Interface

The I/O Engine supports a module interface that allows to load server-side code on demand. Each module is effectively a library dynamically loaded by the I/O Engine via dlopen.
The interface between the module and the I/O Engine is defined in the `dss_module` data structure.

Each module should specify:
- a module name
- a module identifier from `daos_module_id`
- a feature bitmask
- a module initialization and finalize function

In addition, a module can optionally configure:
- a setup and cleanup function invoked once the overall stack is up and running
- CART RPC handlers
- dRPC handlers

## Thread Model & Argobot Integration

The I/O Engine is a multi-threaded process using Argobots for non-blocking processing.

By default, one main xstream and no offload xstreams are created per target. The actual number of offload xstream can be configured through daos_engine command line parameters. Moreover, an extra xstream is created to handle incoming metadata requests. Each xstream is bound to a specific CPU core. The main xstream is the one receiving incoming target requests from both client and the other servers. A specific ULT is started to make progress on network and NVMe I/O operations.

## Thread-local Storage (TLS)

Each xstream allocates private storage that can be accessed via the `dss_tls_get()` function. When registering, each module can specify a module key with a size of data structure that will be allocated by each xstream in the TLS. The `dss_module_key_get()` function will return this data structure for a specific registered module key.

## Incast Variable Integration

DAOS uses IV (incast variable) to share values and statuses among servers under a single IV namespace, which is organized as a tree. The tree root is called IV leader, and servers can either be leaves or non-leaves. Each server maintains its own IV cache. During fetch, if the local cache can not fulfill the request, it forwards the request to its parents, until reaching the root (IV leader). As for update, it updates its local cache first, then forwards to its parents until it reaches the root, which then propagate the changes to all the other servers. The IV namespace is per pool, which is created during pool connection, and destroyed during pool disconnection. To use IV, each user needs to register itself under the IV namespace to get an identification, then it will use this ID to fetch or update its own IV value under the IV namespace.

## dRPC Server

The I/O Engine includes a dRPC server that listens for activity on a given Unix Domain Socket. See the [dRPC documentation](../control/drpc/README.md) for more details on the basics of dRPC, and the low-level APIs in Go and C.

The dRPC server polls periodically for incoming client connections and requests. It can handle multiple simultaneous client connections via the `struct drpc_progress_context` object, which manages the `struct drpc` objects for the listening socket as well as any active client connections.

The server loop runs in its own User-Level Thread (ULT) in xstream 0. The dRPC socket has been set up as non-blocking and polling uses timeouts of 0, which allows the server to run in a ULT rather than its own xstream. This channel is expected to be relatively low-traffic.

### dRPC Progress

`drpc_progress` represents one iteration of the dRPC server loop. The workflow is as follows:

1. Poll with a timeout on the listening socket and any open client connections simultaneously.
2. If any activity is seen on a client connection:
    1. If data has come in: Call `drpc_recv` to process the incoming data.
    2. If the client has disconnected or the connection has been broken: Free the `struct drpc` object and remove it from the `drpc_progress_context`.
3. If any activity is seen on the listener:
    1. If a new connection has come in: Call `drpc_accept` and add the new `struct drpc` object to the client connection list in the `drpc_progress_context`.
    2. If there was an error: Return `-DER_MISC` to the caller. This causes an error to be logged in the I/O Engine, but does not interrupt the dRPC server loop. Getting an error on the listener is unexpected.
4. If no activity was seen, return `-DER_TIMEDOUT` to the caller. This is purely for debugging purposes. In practice the I/O Engine ignores this error code, since lack of activity is not actually an error case.

### dRPC Handler Registration

Individual DAOS modules may implement handling for dRPC messages by registering a handler function for one or more dRPC module IDs.

Registering handlers is simple. In the `dss_server_module` field `sm_drpc_handlers`, statically allocate an array of `struct dss_drpc_handler` with the last item in the array zeroed out to indicate the end of the list. Setting the field to NULL indicates nothing to register. When the I/O Engine loads the DAOS module, it will register all of the dRPC handlers automatically.

**Note:** The dRPC module ID is **not** the same as the DAOS module ID. This is because a given DAOS module may need to register more than one dRPC module ID, depending on the functionality it covers. The dRPC module IDs must be unique system-wide and are listed in a central header file: `src/include/daos/drpc_modules.h`

The dRPC server uses the function `drpc_hdlr_process_msg` to handle incoming messages. This function checks the incoming message's module ID, searches for a handler, executes the handler if one is found, and returns the `Drpc__Response`. If none is found, it generates its own `Drpc__Response` indicating the module ID was not registered.

    
    =====================================================================================
    
    
# DFS Overview

DFS stands for DAOS File System. The DFS API provides an encapsulated namespace
with a POSIX-like API directly on top of the DAOS API. The namespace is
encapsulated under a single DAOS container, where directories and files are
objects in that container.

The encapsulated namespace will be located in one DAOS Pool and a single DAOS
Container. The user provides a valid (connected) pool handle and an open
container handle where the namespace will be located.

## DFS Namespace

When the file system is created (i.e. when the DAOS container is initialized as
an encapsulated namespace), a reserved object (with a predefined object ID) will
be added to the container and will record superblock (SB) information about the
namespace. The SB object has the reserved OID 0.0. The object class is
determined either through the oclass parameter passed to container creation or
through automatic selection based on container properties such as the redundancy
factor.

The SB object contains an entry with a magic value to indicate it is a POSIX
filesystem. The SB object will contain also an entry to the root directory of
the filesystem, which will be another reserved object with a predefined OID
(1.0) and will have the same representation as a directory (see next
section). The OID of the root id will be inserted as an entry in the superblock
object.

The SB will look like this:

~~~~
D-key: "DFS_SB_METADATA"
A-key: "DFS_MAGIC"
single-value (uint64_t): SB_MAGIC (0xda05df50da05df50)

A-key: "DFS_SB_VERSION"
single-value (uint16_t): Version number of the SB. This is used to determine the layout of the SB (the DKEYs and value sizes).

A-key: "DFS_LAYOUT_VERSION"
single-value (uint16_t): This is used to determine the format of the entries in the DFS namespace (DFS to DAOS mapping).

A-key: "DFS_SB_FEAT_COMPAT"
single-value (uint64_t): flags to indicate feature set like extended attribute support, indexing

A-key: "DFS_SB_FEAT_INCOMPAT"
single-value (uint64_t): flags

A-key: "DFS_SB_MKFS_TIME"
single-value (uint64_t): time when DFS namespace was created

A-key: "DFS_SB_STATE"
single-value (uint64_t): state of FS (clean, corrupted, etc.)

A-key: "DFS_CHUNK_SIZE"
single-value (uint64_t): Default chunk size for files in this container

A-key: "DFS_OBJ_CLASS"
single-value (uint16_t): Default object class for files in this container

D-key: "/"
// rest of akey entries for root are same as in directory entry described below.
~~~~~~

## DFS Directories

A POSIX directory will map to a DAOS object with multiple dkeys, where each dkey
will correspond to an entry in that directory (for another subdirectory, regular
file, or symbolic link). The dkey value will be the entry name in that
directory. The dkey will contain an akey with all attributes of that entry in a
byte array serialized format. Extended attributes will each be stored in a
single value under a different akey. The mapping table will look like this
(includes two extended attributes: xattr1, xattr2):

~~~~~~
Directory Object
  D-key "entry1_name"
    A-key "DFS_INODE"
      RECX (byte array starting at idx 0):
        mode_t: permission bit mask + type of entry
        oid: object id of entry
        atime: access time
        mtime: modify time
        ctime: change time
        chunk_size: chunk_size of file (0 if default or not a file)
        syml: symlink value (does not exist if not a symlink)
    A-key "x:xattr1"	// extended attribute name (if any)
    A-key "x:xattr2"	// extended attribute name (if any)
~~~~~~

The extended attributes are all prefixed with "x:".

This summarizes the mapping of a directory testdir with a file, directory, and
symlink:

~~~~~~
testdir$ ls
dir1
file1
syml1 -> dir1
(目录的Object，内部是这样的结构)
Object testdir
  D-key "dir1"
    A-key "mode" , permission bits + S_IFDIR
    A-key "oid" , object id of dir1
    ...
  D-key "file1"
    A-key "mode" , permission bits + S_IFREG
    A-key "oid" , object id of file1
    ...
  D-key "syml1"
    A-key "mode" , permission bits + S_IFLNK
    A-key "oid" , empty
    A-key "syml", dir1
    ...
~~~~~~

Note that with this mapping, the inode information is stored with the entry that
it corresponds to in the parent directory object. Thus, hard links won't be
supported, since it won't be possible to create a different entry (dkey) that
actually points to the same set of akeys that the current ones are stored
within. This limitation was agreed upon, and makes the representation simple as
described above.

## Files

As shown in the directory mapping above, the entry of a file will be inserted in
its parent directory object with an object ID that corresponds to that file. The
object ID for a regular file will be of a DAOS array object, which itself is a
DAOS object with some properties (the element size and chunk size). In the
POSIX file case, the cell size will always be 1 byte. The chunk size can be set
at create time only, with the default being 1 MB. The array object itself is
mapped onto a DAOS object with integer dkeys, where each dkey contains
chunk_size elements. So for example, if we have a file with size 10 bytes, and
chunk size is 3 bytes, the array object will contain the following:
(文件是Array Object，内部有多个Chunks of Cells)
(文件的Object，内部是这样的结构)

~~~~
Object array
  D-key 0
    A-key NULL , array elements [0,1,2]
  D-key 1
    A-key NULL , array elements [3,4,5]
  D-key 2
    A-key NULL , array elements [6,7,8]
  D-key 3
    A-key NULL , array elements [9]
~~~~~~

For more information about the array object layout, please refer to the
README.md file for Array Addons.

Access to that object is done through the DAOS Array API. All read and write
operations to the file will be translated to DAOS array read and write
operations. The file size can be set (truncate) or retrieved by the DAOS array
set_size/get_size functions. Increasing the file size however in this case, does not
guarantee that space is allocated. Since DAOS logs I/Os across different epochs,
space allocation cannot be supported by a naïve set_size operation.

## Symbolic Links

As mentioned in the directory section, symbolic links will not have an object
for the symlink itself, but will have a value in the entry itself of the parent
directory containing the actual value of the symlink.

## Access Permissions

All DFS objects (files, directories, and symlinks) inherit the access
permissions of the DFS container that they are created with. So the permission
checks are done on dfs_mount(). If that succeeds and the user has access to the
container, then they will be able to access all objects in the DFS
namespace.

setuid(), setgid() programs, supplementary groups, ACLs are not supported in the
DFS namespace.

    
    =================================================================================
    
