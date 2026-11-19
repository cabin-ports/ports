#!/usr/bin/env bash
# Temporary CI probe (removed before merge): the MySQL and Redis servers the
# check-linux job starts, over TCP, and MySQL over TLS.
set -euo pipefail
mysql --version
mysql --user=root --password=root --host=127.0.0.1 --ssl-mode=REQUIRED --table \
  --execute "SELECT VERSION(); SHOW VARIABLES LIKE 'have_ssl'; SHOW SESSION STATUS LIKE 'Ssl_version'; SHOW SESSION STATUS LIKE 'Ssl_cipher'; SELECT user, host, plugin FROM mysql.user"
redis-server --version
redis-cli -h 127.0.0.1 ping
redis-cli -h 127.0.0.1 hello 3
