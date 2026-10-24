## Importing the official DDNet Database

```sh
$ wget https://ddnet.org/stats/ddnet-sql.zip # (~2.5 GiB)
$ unzip ddnet-sql.zip
$ yay -S mariadb mysql-connector-c++
$ sudo mysql_install_db --user=mysql --basedir=/usr --datadir=/var/lib/mysql
$ sudo systemctl start mariadb
$ sudo mysqladmin -u root password 'PW'
$ mysql -u root -p'PW'
MariaDB [(none)]> create database teeworlds; create user 'teeworlds'@'localhost' identified by 'PW2'; grant all privileges on teeworlds.* to 'teeworlds'@'localhost'; flush privileges;
# this takes a while, you can remove the KEYs in record_race.sql to trade performance in queries
$ mysql -u teeworlds -p'PW2' teeworlds < ddnet-sql/record_*.sql

$ cat mine.cfg
sv_use_sql 1
add_sqlserver r teeworlds record teeworlds "PW2" "localhost" "3306"
add_sqlserver w teeworlds record teeworlds "PW2" "localhost" "3306"

$ cmake -Bbuild -DMYSQL=ON -GNinja
$ cmake --build build --target DDNet-Server
$ build/DDNet-Server -f mine.cfg
```

## Account-linked race records

New solo and team finishes store the logged-in account username alongside each player's name. Finishes made while logged out, existing records, and manual `record_insert` entries remain unlinked. Disabling an account with `acc_disable` hides its linked solo records from rankings and time queries. If any member of a team record has a disabled or missing linked account, the whole team record is hidden. Re-enabling the account makes those records visible again.

Database setup adds the account column to existing race tables, creates `record_race_deleted`, and creates the `record_race_visible` and `record_teamrace_visible` views used by score reads. MySQL uses the configured table prefix instead of `record`.

The `record_remove`, `record_remove_time`, and `record_remove_all` commands move matching solo records to the deleted table. Use `record_restore`, `record_restore_time`, and `record_restore_all` with the same arguments to restore them. The remove and restore commands update name-based points. Account disabling does not change points totals because points are stored by player name, not account.
