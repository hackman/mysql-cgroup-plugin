#!/bin/bash

db_name=cgroup_limits


if ! mysql -e 'SHOW DATABASES' |grep -q $db_name; then
	mysql -e "CREATE DATABASE $db_name"
fi

if ! mysql -e 'SHOW TABLES LIKE limits'|grep -q limits; then
	mysql -e '
CREATE TABLE limits (
	id integer,
	user varchar(40),
	host varchar(254),
	cpu double,
	reads integer,
	writes integer,
	mem integer
);
' $db_name

