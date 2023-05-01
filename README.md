# QuickChunk

QuickChunk is a tool designed to synchronize a large (slightly changed) file from
a client to a server. Its primary use-case is the update of a hard disk image on
a backup server.

QuickChunk's speed in syncing a file comes from the similarities between the file
on the server and the client.

In essence, QuickChunk reads the large file into fixed-sized chunks in memory,
both on the client and the server. Then it compares these chunks one by one
and transmits only those that differ to the server. This simple approach significantly
reduces network traffic and improves performance, making QuickChunk a practical
solution for handling large files that slightly change content.

To quickly identify the differing chunks, it employs the XXH3 algorithm, which
calculates 128-bit hashes for each chunk.

## Potential Use Case

The synchronization of entire hard drive images: For instance, it can be integrated
within an Initramfs to sync the full hard drive to a server before the system
fully boots up. So with solid NVME drives, the author's 1TB usually gets synced
in about 10 minutes.

A robust backup solution, for example, could involve using
[BorgBackup](https://www.borgbackup.org/) for daily backups of specific directories
like '/home', alongside weekly full-image backups with QuickChunk.

## Drawbacks/TODOs

* chunks are currently hard coded and fixed in size (optimized for LAN usage)
* The chunk size should be variable and self-adaptive, enabling the utilization
  of a streaming hash calculation with XXH3.
* The network protocol and the way the client and server interact could be optimized
  for increased efficiency and flexibility.
* file size needs to be the same on server and client
* network traffic is non encrypted (needs e.g. ssh tunnel for non LAN usage)
* server-mode: rx and write to disk is sequential, should be streamed instead

## Why Not Rsync?

Rsync is a powerful tool for file synchronization, however, when handling very
large files like full hard disk or VM image files, it may face challenges.
As discussed in rsync's GitHub issue [#217](https://github.com/WayneD/rsync/issues/217)
(you might want to vote for it), it can struggle to efficiently handle very large
files because of its checksum searching algorithm.

QuickChunk, on the other hand, is designed to handle large files. It focuses on
transferring only the different fixed-size chunks of data, which increases efficiency
when dealing with such large files. While rsync continues to be an excellent choice
for many scenarios, QuickChunk provides a complementary solution for this particular
use case.

## Similar Tools

* https://github.com/theraser/blocksync
* https://borgbackup.readthedocs.io/en/stable/deployment/image-backup.html

## QuickChunk: Its Past, Present, and Future

### Lookback

This tool was developed as a learning experience and as a workaround to rsync's
current limitations with large files.

### Current Status

QuickChunk fulfills the author's need for the efficient synchronization of large
disk images, thereby complementing a [BorgBackup](https://www.borgbackup.org/)
strategy. In the case of system failure, it allows for rapid recovery.

### Outlook

Once rsync's large file handling issue ([#217](https://github.com/WayneD/rsync/issues/217))
is fixed, this tool could become obsolete.

## Prerequisites

Before running QuickChunk, make sure you have the following libraries installed:

* Glib
* Gio
* XXHash

These dependencies can usually be installed using a package manager such as apt,
pacman, or brew. For instance, on a Debian-based system you could use:

```bash
sudo apt-get install libglib2.0-dev libglib2.0-0 libxxhash-dev
```

## Building

QuickChunk uses CMake as its build system. To build the program, follow these
steps:

1. Create a build directory and navigate into it:
    ```bash
    mkdir build
    cd build
    ```

2. Run CMake to configure the build and generate a Makefile:
    ```bash
    cmake ..
    ```

3. Build the program:
    ```bash
    make
    ```

## Usage

To use QuickChunk, you can pass the command-line options described below:

* `--server` or `-s`: Run in server mode.
* `--ip` or `-i`: IP address to use.
* `--port` or `-p`: Port to use.
* `--file` or `-f`: File to use.
* `--verbose` or `-v`: Increase verbosity (-vv is for debug)

To run the program in server mode:

```
./quickchunk -s -i <IP_ADDRESS> -p <PORT> -f <FILENAME>
```

To run the program in client mode:

```
./quickchunk -i <SERVER_IP_ADDRESS> -p <SERVER_PORT> -f <FILENAME_TO_SEND>
```

## Testing throughput

```bash
dd if=/dev/disk/by-id/nvme-Seagate_FireCuda_530_ZP1000GM30013_XXXXXXXX of=/dev/null bs=4096 status=progress
```
```
18536103936 bytes (19 GB, 17 GiB) copied, 64 s, 290 MB/s
```

This is considered a slow read rate for an NVME SSD. Consider eliminating these
kinds of bottlenecks first, for instance, by using a high-performance NVME drive.

## Note

This project is licensed under the terms of the GNU GPL-3.0-or-later license.
Please see the LICENSE file in this directory for more information.

## Author

Christoph Fritz
