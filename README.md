# What is this?

This is a MariaDB plugin that allows to limit all connections of a user to certain CPU and/or I/O BW.

To achieve that, the plugin is built as an audit plugin.

It will check if /sys/fs/cgroup/{cpu,memory,blkio}/mysql cgroups exist and if so, 
it will create new cgroups per-user under them. 


This will allow administrators to control the CPU and I/O utilization per-user
by imposing proper limits for all of their connections.

In the future it may also include network BW limits.

# Build
The current version is prepared to be build against MariaDB.
Future versions would be compatible with MySQL and Percona.

```
cd mysql-cgroup-plugin
make
```

# Setup

After install of MySQL/MariaDB you need to run the setup_cgroups.sh script.
It will create the cgroup_limits database and the limits table inside of it.

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
MariaDB [(none)]> INSTALL PLUGIN audit_cgroup SONAME 'audit_cgroup.so';
```

Verify that the plugin is loaded and enabled:
```
MariaDB [(none)]> SHOW VARIABLES LIKE 'audit_cgroup_enabled';
+----------------------+-------+
| Variable_name        | Value |
+----------------------+-------+
| audit_cgroup_enabled | ON    |
+----------------------+-------+
1 row in set (0.003 sec)

```

# Maintance
You can temporary disable the plugin:
```
MariaDB [(none)]> set global audit_cgroup_enabled=OFF;
Query OK, 0 rows affected (0.000 sec)

MariaDB [(none)]> show variables like 'audit_cgroup_enabled';
+----------------------+-------+
| Variable_name        | Value |
+----------------------+-------+
| audit_cgroup_enabled | OFF   |
+----------------------+-------+
1 row in set (0.001 sec)
```
and then reenable it:
```
MariaDB [(none)]> set global audit_cgroup_enabled=ON;
Query OK, 0 rows affected (0.000 sec)

```

Removing the plugin completely:
```
UNINSTALL SONAME 'audit_cgroup.so';
```

