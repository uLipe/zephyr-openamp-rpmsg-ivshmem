OpenAMP RPMSG over IVSHMEM test app
####################################

Prerequisites
*************

* Tested with Zephyr version 3.4.0-rc1.

* Tested agains Zephyr SDK 0.16.1

* QEMU needs to available.

ivshmem-server needs to be available and running. The server is available in
Zephyr SDK or pre-built in some distributions. Otherwise, it is available in
QEMU source tree.

ivshmem-client needs to be available as it is employed in this sample as an
external application. The same conditions of ivshmem-server applies to the
ivshmem-server, as it is also available via QEMU.


Preparing IVSHMEM server before doing anything:
***********************************************

#. The ivshmem-server utillity for QEMU can be found into the zephyr sdk
   directory, in:
   ``/path/to/your/zephyr-sdk/zephyr-<version>/sysroots/x86_64-pokysdk-linux/usr/xilinx/bin/``

#. You may also find ivshmem-client utillity, it can be useful to debug if everything works
   as expected.

#. Run ivshmem-server. For the ivshmem-server, both number of vectors and
   shared memory size are decided at run-time (when the server is executed).
   For Zephyr, the number of vectors and shared memory size of ivshmem are
   decided at compile-time and run-time, respectively.For Arm64 we use
   vectors == 2 for the project configuration in this sample. Here is an example:

   .. code-block:: console

      # n = number of vectors
      $ sudo ivshmem-server -n 2
      $ *** Example code, do not use in production ***

#. Appropriately set ownership of ``/dev/shm/ivshmem`` and
   ``/tmp/ivshmem_socket`` for your deployment scenario. For instance:

   .. code-block:: console

      $ sudo chgrp $USER /dev/shm/ivshmem
      $ sudo chmod 060 /dev/shm/ivshmem
      $ sudo chgrp $USER /tmp/ivshmem_socket
      $ sudo chmod 060 /tmp/ivshmem_socket

Building and Running
********************
There are instance_1 and instance_2 folders, use the --build-dir or -d options to get two
IVSHMEM Zephyr enabled apps:

   .. code-block:: console
      $ west build -pauto -bqemu_cortex_a53 path/to/this-repo -d path/to/this-repo/instance_1
      $ west build -pauto -bqemu_cortex_a53 path/to/this-repo -d path/to/this-repo/instance_2

* Note: Warnings that appear are from ivshmem-shell subsystem and can be ignored.

After getting the both appplications built, open two terminals and run each
instance separately.

For example to run instance 1:

   .. code-block:: console

      $ west build -t run path/to/this-repo -d path/to/this-repo/instance_1

For instance 2, just go to the other build directory in another terminal:

   .. code-block:: console

      $ west build -t run path/to/this-repo -d path/to/this-repo/instance_2

Expected output:
****************






