#!/bin/bash
cgroups='cpu memory blkio'

for cg in $cgroups; do
	if ! grep -cE "^$i\s.*" /proc/cgroups; then
		echo "Missing cgroup support for: $cg"
		exit 1
	fi
	if ! grep "cgroup/$cg " /proc/mounts; then
		mkdir -p /sys/fs/cgroup/$i 2>/dev/null
		mount -t cgroup -o $cg $cg /sys/fs/cgroup/$i
	fi
done
mkdir /sys/fs/cgroup/{cpu,memory,blkio}/mysql

# We need to make them available to mysql, so it can put the threads in the cgroups
chown mysql: /sys/fs/cgroup/{cpu,memory,blkio}/mysql

