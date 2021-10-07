# What is this?

This is a MariaDB audit_cgroup plugin. 

It will check if /sys/fs/cgroup/{cpu,memory,blkio}/mysql cgroups exist and if so, 
it will create new cgroups per-user under them. 
This allows the administrator to control the CPU and Blkio utilization per-user.

# Build
The current version is prepared to be build against MariaDB.
Future versions would be compatible with MySQL and Percona.

```
cd mysql-cgroup-plugin
make
```

# Setup

After install of MySQL/MariaDB you need to run the setup_cgroups.sh script, that will 
create the cgroup_limits database and the limits table inside of it.

You should then move the mysql_init_cgroups.sh to /usr/local/sbin.

In order to have automatic initialization of the cgroups you can either setup mysql slice
in systemd or use the provided mysql_init_cgroups.sh script.
If you want to use the provided script, simply copy systemd/cgroups.conf to:
```
/etc/systemd/system/mariadb.service.d/
```
This will add execution of the script, before each start of MariaDB.
The important part is that the script chowns the newly created mysql cgroups, so the 
mysql daemon can create new groups and update current values.

Next you need to copy the audit_cgroup.so to the plugin directory of your mysql installation.

Finally, you need to load the plugin:
```
INSTALL PLUGIN audit_cgroup SONAME 'audit_cgroup.so';
```

Verigy that the plugin is loaded and enabled:
```
SHOW VARIABLES LIKE 'audit_cgroup_enabled';
```
