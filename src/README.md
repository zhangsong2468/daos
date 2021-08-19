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

CaRT is an open-source RPC transport layer for Big Data and Exascale HPC. It supports both traditional P2P RPC delivering and collective RPC which invokes the RPC at a group of target servers with a scalable tree-based message propagating.

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
